#include <memory>
#include <filesystem>
#include <array>
#include <future>
#include <thread>

#include <QApplication>
#include <gtest/gtest.h>

#include "fake_client_module_services.h"
#include "px_client/modules/clipboard/clipboard_module.h"
#include "px_client/modules/clipboard/clipboard_runtime_bridge.h"
#include "px_client/modules/clipboard/win/cp_virtual_file.h"
#include "px_common/data.h"
#include "px_message.pb.h"

namespace px {
namespace {

std::shared_ptr<Message> BufferRequest(std::string name, std::int64_t size = 16, std::int64_t offset = 0) {
    auto message = std::make_shared<Message>();
    message->set_type(kClipboardReqBuffer);
    auto& request = *message->mutable_cp_req_buffer();
    request.set_full_name(std::move(name));
    request.set_req_index(0);
    request.set_req_start(offset);
    request.set_req_size(size);
    return message;
}

TEST(ClientClipboardModuleLifecycle, OnlyCurrentPublishedFileMayBeReadWithinAdvertisedSize) {
    const auto services = std::make_shared<test::FakeClientModuleServices>();
    auto bridge = std::make_shared<ClipboardRuntimeBridge>(services);
    auto settings = test::MakeModuleConfig("clipboard-validation").settings_;
    settings.clipboard_enabled_ = true;
    bridge->Activate(settings);
    const auto local_path = std::filesystem::path(__FILE__).generic_string();
    ASSERT_TRUE(std::filesystem::exists(local_path));
    bridge->OnRequestFileBuffer(BufferRequest(local_path));
    EXPECT_EQ(services->file_messages_.load(), 0);

    ClipboardFile file{};
    file.set_file_name("fixture.cpp");
    file.set_full_path(local_path);
    file.set_total_size(4); // Actual source is larger: only the advertised four bytes may leave the process.
    bridge->SendClipboardUpdate(kClipboardFiles, {}, {file});
    ASSERT_EQ(services->ClipboardFiles().size(), 1U);
    const auto offer = services->ClipboardFiles().front().full_path();
    EXPECT_TRUE(offer.starts_with("pixels-clipboard://"));
    EXPECT_NE(offer, local_path);
    bridge->OnRequestFileBuffer(BufferRequest(local_path));
    bridge->OnRequestFileBuffer(BufferRequest(offer, -1));
    bridge->OnRequestFileBuffer(BufferRequest(offer, 256 * 1024));
    bridge->OnRequestFileBuffer(BufferRequest(offer, 1, 5));
    EXPECT_EQ(services->file_messages_.load(), 0);
    bridge->OnRequestFileBuffer(BufferRequest(offer));
    ASSERT_EQ(services->FilePayloads().size(), 1U);
    Message response{};
    ASSERT_TRUE(response.ParseFromString(services->FilePayloads().front()->AsString()));
    EXPECT_EQ(response.cp_resp_buffer().read_size(), 4);
    EXPECT_EQ(response.cp_resp_buffer().buffer().size(), 4U);
    EXPECT_EQ(response.cp_resp_buffer().req_size(), 16);

    bridge->SendClipboardUpdate(kClipboardFiles, {}, {file});
    const auto replacement = services->ClipboardFiles().front().full_path();
    EXPECT_NE(replacement, offer);
    bridge->OnRequestFileBuffer(BufferRequest(offer));
    EXPECT_EQ(services->file_messages_.load(), 1);
    bridge->SendClipboardUpdate(kClipboardText, "replacement text", {});
    bridge->OnRequestFileBuffer(BufferRequest(replacement));
    EXPECT_EQ(services->file_messages_.load(), 1);
    bridge->SendClipboardUpdate(kClipboardFiles, {}, {file});
    const auto last = services->ClipboardFiles().front().full_path();
    bridge->Deactivate();
    bridge->OnRequestFileBuffer(BufferRequest(last));
    EXPECT_EQ(services->file_messages_.load(), 1);
}

TEST(ClientClipboardModuleLifecycle, RepeatedStartQueueStopRejectsLateWork) {
    const auto services =
        std::make_shared<test::FakeClientModuleServices>();
    for (int round = 0; round < 10; ++round) {
        const auto module = std::make_shared<ClientClipboardModule>(
            std::weak_ptr<ClientModuleServices>(services));
        ASSERT_TRUE(module->Start(test::MakeModuleConfig(
            "clipboard-module-" + std::to_string(round))));
        for (int index = 0; index < 64; ++index) {
            auto message = std::make_shared<Message>();
            message->set_type(MessageType::kClipboardReqAtBegin);
            message->mutable_cp_req_at_begin()->set_full_name(
                "clipboard-queued-" + std::to_string(index));
            module->HandleMessage(message);
        }
        module->Stop();
        module->Stop();
        const auto begins_at_stop = services->transfer_begins_.load();

        auto late_message = std::make_shared<Message>();
        late_message->set_type(MessageType::kClipboardReqAtEnd);
        late_message->mutable_cp_req_at_end()->set_full_name("late");
        late_message->mutable_cp_req_at_end()->set_success(true);
        module->HandleMessage(late_message);
        EXPECT_EQ(services->transfer_begins_.load(), begins_at_stop);
    }
}

void VerifyVirtualFileCompletion(bool read_all, HRESULT operation_result, bool expected_success) {
    using namespace std::chrono_literals;
    const auto services = std::make_shared<test::FakeClientModuleServices>();
    const auto bridge = std::make_shared<ClipboardRuntimeBridge>(services);
    auto settings = test::MakeModuleConfig("clipboard-ole-completion").settings_;
    settings.clipboard_enabled_ = true;
    bridge->Activate(settings);
    const auto virtual_file = CreateVirtualFile(bridge);
    ASSERT_TRUE(virtual_file);
    ClipboardFile file{};
    file.set_file_name("remote.bin");
    file.set_ref_path("remote.bin");
    file.set_full_path("pixels-clipboard://remote/0");
    file.set_total_size(4);
    virtual_file->OnClipboardFilesInfo({file});
    FORMATETC format{};
    format.cfFormat = static_cast<CLIPFORMAT>(RegisterClipboardFormatW(L"FileContents"));
    format.dwAspect = DVASPECT_CONTENT;
    format.lindex = 0;
    format.tymed = TYMED_ISTREAM;
    STGMEDIUM medium{};
    ASSERT_EQ(virtual_file->GetData(&format, &medium), S_OK);
    Microsoft::WRL::ComPtr<IStream> stream{};
    stream.Attach(medium.pstm); // NOLINT(gammaray-raw-pointer-boundary): adopt the COM output immediately, no retained ABI alias.
    medium.pstm = nullptr;
    if (read_all) {
        const auto completed = std::make_shared<std::promise<HRESULT>>();
        auto future = completed->get_future();
        std::jthread worker([stream, completed] {
            std::array<char, 4> bytes{};
            ULONG count{};
            const auto result = stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &count);
            completed->set_value(result == S_OK && count == 4 ? S_OK : E_FAIL);
        });
        const auto deadline = std::chrono::steady_clock::now() + 2s;
        while (services->file_messages_.load() == 0 && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::sleep_for(5ms);
        }
        if (services->file_messages_.load() == 0) {
            virtual_file->ExitAllStreams();
            FAIL() << "The OLE stream did not submit its request";
        }
        ClipboardRespBuffer response{};
        response.set_full_name(file.full_path());
        response.set_req_index(0);
        response.set_req_size(4);
        response.set_read_size(4);
        response.set_buffer("data");
        virtual_file->OnClipboardRespBuffer(response);
        ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
        EXPECT_EQ(future.get(), S_OK);
    }
    EXPECT_EQ(virtual_file->EndOperation(operation_result, nullptr, 0), S_OK);
    ASSERT_EQ(services->TransferResults().size(), 1U);
    EXPECT_EQ(services->TransferResults().front(), expected_success);
    virtual_file->ExitAllStreams();
    EXPECT_EQ(services->TransferResults().size(), 1U);
    bridge->Deactivate();
}

TEST(ClientClipboardModuleLifecycle, OleSuccessRequiresCompleteReadAndSuccessfulOperation) {
    VerifyVirtualFileCompletion(false, S_OK, false);
    VerifyVirtualFileCompletion(true, E_FAIL, false);
    VerifyVirtualFileCompletion(true, S_OK, true);
}

} // namespace
} // namespace px

int main(int argc, char** argv) {  // NOLINT(gammaray-raw-pointer-boundary): process entry ABI
    QApplication application(argc, argv);
    testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
