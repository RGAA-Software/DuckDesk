#include "px_android/core-native/src/main/cpp/native_clipboard.h"
#include "px_common/data.h"
#include "px_common/uuid.h"

#include <filesystem>
#include <fstream>
#include <future>
#include <iterator>
#include <gtest/gtest.h>

namespace pixels::android {
namespace {

using namespace std::chrono_literals;

struct TemporaryClipboardDirectory final {
    const std::filesystem::path path{std::filesystem::temp_directory_path() / ("pixels-clipboard-test-" + px::GetUUID())};
    ~TemporaryClipboardDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(path, error);
    }
};

struct DownloadResult final {
    std::vector<std::string> paths{};
    std::string error{};
};

struct ClipboardHarness final : std::enable_shared_from_this<ClipboardHarness> {
    std::shared_ptr<NativeClipboard> clipboard{};
    std::vector<px::Message> sent{};
    std::vector<std::function<void()>> queued{};
    std::shared_ptr<std::promise<DownloadResult>> completed{std::make_shared<std::promise<DownloadResult>>()};
    std::string remote_generation{};
    bool queue_tasks{};
    bool answer_download{};
    bool stop_from_send{};

    void Start() {
        const auto weak = weak_from_this();
        clipboard = NativeClipboard::Create(
            "device", "stream",
            [weak](std::shared_ptr<px::Data> bytes) {
                const auto self = weak.lock();
                if (!self) {
                    return false;
                }
                px::Message message{};
                if (!message.ParseFromString(bytes->AsString())) {
                    return false;
                }
                self->sent.push_back(std::move(message));
                return true;
            },
            [weak](std::shared_ptr<px::Data> bytes) {
                const auto self = weak.lock();
                if (!self) {
                    return false;
                }
                px::Message message{};
                if (!message.ParseFromString(bytes->AsString())) {
                    return false;
                }
                self->sent.push_back(message);
                if (message.type() == px::kClipboardReqBuffer && self->stop_from_send) {
                    self->clipboard->Stop();
                    return false;
                }
                if (message.type() == px::kClipboardReqBuffer && self->answer_download) {
                    const auto& request = message.cp_req_buffer();
                    auto response = std::make_shared<px::Message>();
                    response->set_type(px::kClipboardRespBuffer);
                    auto& chunk = *response->mutable_cp_resp_buffer();
                    chunk.set_full_name(request.full_name());
                    chunk.set_req_index(request.req_index());
                    chunk.set_req_start(request.req_start());
                    chunk.set_req_size(request.req_size());
                    chunk.set_read_size(request.req_size());
                    chunk.set_buffer(std::string(static_cast<std::size_t>(request.req_size()), 'x'));
                    auto wrong = std::make_shared<px::Message>(*response);
                    wrong->mutable_cp_resp_buffer()->set_req_start(request.req_start() + 1);
                    self->clipboard->HandleFileMessage(wrong);
                    self->clipboard->HandleFileMessage(response);
                }
                return true;
            },
            [weak](std::function<void()> task) {
                const auto self = weak.lock();
                if (!self) {
                    return false;
                }
                if (self->queue_tasks) {
                    self->queued.push_back(std::move(task));
                } else {
                    task();
                }
                return true;
            },
            [weak](const NativeClipboardFiles& files) {
                if (const auto self = weak.lock()) {
                    self->remote_generation = files.generation;
                }
            },
            [weak](const std::string&, const std::vector<std::string>& paths, const std::string& error) {
                if (const auto self = weak.lock()) {
                    self->clipboard->Stop(); // Intentional own-worker shutdown: must defer the join safely.
                    self->completed->set_value({paths, error});
                }
            });
    }

    void OfferRemote(std::int64_t size) {
        auto message = std::make_shared<px::Message>();
        message->set_type(px::kClipboardInfo);
        auto& info = *message->mutable_clipboard_info();
        info.set_type(px::kClipboardFiles);
        auto& file = *info.add_files();
        file.set_file_name("payload.bin");
        file.set_full_path("pixels-clipboard://remote/0");
        file.set_total_size(size);
        clipboard->AcceptRemoteFiles(message);
    }
};

std::shared_ptr<px::Message> Request(std::string name, std::int64_t offset, std::int64_t size) {
    auto message = std::make_shared<px::Message>();
    message->set_type(px::kClipboardReqBuffer);
    auto& request = *message->mutable_cp_req_buffer();
    request.set_full_name(std::move(name));
    request.set_req_index(0);
    request.set_req_start(offset);
    request.set_req_size(size);
    return message;
}

TEST(NativeClipboardProtocol, PublishedWhitelistRangeAndQueuedWorkAfterStop) {
    const auto harness = std::make_shared<ClipboardHarness>();
    harness->Start();
    ASSERT_TRUE(harness->clipboard);
    const auto path = std::filesystem::path(__FILE__);
    const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path));
    ASSERT_TRUE(harness->clipboard->PublishLocalFiles("offer", {{"fixture.cpp", {}, path.string(), size}}));
    const auto token = harness->sent.front().clipboard_info().files(0).full_path();
    harness->clipboard->HandleFileMessage(Request(path.string(), 0, 16));
    harness->clipboard->HandleFileMessage(Request(token, -1, 16));
    harness->clipboard->HandleFileMessage(Request(token, 0, 256 * 1024));
    EXPECT_EQ(harness->sent.size(), 1U);
    harness->clipboard->HandleFileMessage(Request(token, size - 2, 16));
    ASSERT_EQ(harness->sent.size(), 2U);
    EXPECT_EQ(harness->sent.back().cp_resp_buffer().read_size(), 2);
    EXPECT_EQ(harness->sent.back().cp_resp_buffer().req_size(), 16);
    harness->queue_tasks = true;
    harness->clipboard->HandleFileMessage(Request(token, 0, 16));
    harness->clipboard->Stop();
    for (const auto& task : harness->queued) {
        task();
    }
    EXPECT_EQ(harness->sent.size(), 2U);
    harness->clipboard->Stop();
}

TEST(NativeClipboardProtocol, RealMultiChunkDownloadRejectsWrongOffsetAndStopsFromCompletion) {
    const auto harness = std::make_shared<ClipboardHarness>();
    harness->answer_download = true;
    harness->Start();
    const auto size = px::kClipboardReadChunkBytes + 9;
    harness->OfferRemote(size);
    auto future = harness->completed->get_future();
    const TemporaryClipboardDirectory destination{};
    ASSERT_TRUE(harness->clipboard->DownloadRemoteFiles(harness->remote_generation, destination.path.string()));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    EXPECT_TRUE(result.error.empty());
    ASSERT_EQ(result.paths.size(), 1U);
    EXPECT_EQ(std::filesystem::file_size(result.paths.front()), size);
    std::ifstream input(result.paths.front(), std::ios::binary);
    const std::string bytes{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    EXPECT_EQ(bytes, std::string(static_cast<std::size_t>(size), 'x'));
}

TEST(NativeClipboardProtocol, RepeatedHostGenerationStillRotatesWireOfferAndTextRevokesRemoteFiles) {
    const auto harness = std::make_shared<ClipboardHarness>();
    harness->Start();
    const auto path = std::filesystem::path(__FILE__);
    const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path));
    const std::vector<NativeClipboardFile> files{{"fixture.cpp", {}, path.string(), size}};
    ASSERT_TRUE(harness->clipboard->PublishLocalFiles("same-ui-generation", files));
    const auto old = harness->sent.back().clipboard_info().files(0).full_path();
    ASSERT_TRUE(harness->clipboard->PublishLocalFiles("same-ui-generation", files));
    const auto current = harness->sent.back().clipboard_info().files(0).full_path();
    EXPECT_NE(old, current);
    harness->clipboard->HandleFileMessage(Request(old, 0, 8));
    EXPECT_EQ(harness->sent.size(), 2U);
    harness->clipboard->RevokeLocalFiles();
    harness->clipboard->HandleFileMessage(Request(current, 0, 8));
    EXPECT_EQ(harness->sent.size(), 2U);
    harness->OfferRemote(8);
    const auto generation = harness->remote_generation;
    auto text = std::make_shared<px::Message>();
    text->set_type(px::kClipboardInfo);
    text->mutable_clipboard_info()->set_type(px::kClipboardText);
    text->mutable_clipboard_info()->set_msg("text");
    harness->clipboard->AcceptRemoteFiles(text);
    const TemporaryClipboardDirectory destination{};
    EXPECT_FALSE(harness->clipboard->DownloadRemoteFiles(generation, destination.path.string()));
    EXPECT_FALSE(std::filesystem::exists(destination.path));
}

TEST(NativeClipboardProtocol, StopFromSendCallbackCancelsDownloadWithoutSelfJoin) {
    for (int round{}; round < 10; ++round) {
        const auto harness = std::make_shared<ClipboardHarness>();
        harness->stop_from_send = true;
        harness->Start();
        harness->OfferRemote(64);
        auto future = harness->completed->get_future();
        const TemporaryClipboardDirectory destination{};
        ASSERT_TRUE(harness->clipboard->DownloadRemoteFiles(harness->remote_generation, destination.path.string()));
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
        const auto result = future.get();
        EXPECT_FALSE(result.error.empty());
        EXPECT_TRUE(result.paths.empty());
        EXPECT_FALSE(std::filesystem::exists(destination.path / "payload.bin"));
    }
}

} // namespace
} // namespace pixels::android
