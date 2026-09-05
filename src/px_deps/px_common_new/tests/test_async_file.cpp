#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "../async_file.h"
#include "../path_codec.h"

namespace px {
namespace {

using namespace std::chrono_literals;

class TemporaryFilePath final {
public:
    explicit TemporaryFilePath(std::string_view utf8_name) {
        const auto name = PathFromUtf8(utf8_name);
        if (name) {
            path_ = std::filesystem::temp_directory_path() / name.Value();
        }
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    ~TemporaryFilePath() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Get() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

std::shared_ptr<const PxByteBuffer> BytesFromString(std::string_view value) {
    auto bytes = std::make_shared<PxByteBuffer>();
    bytes->reserve(value.size());
    for (const auto character : value) {
        bytes->push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
    return bytes;
}

std::string StringFromBytes(const PxByteBuffer& bytes) {
    std::string value;
    value.reserve(bytes.size());
    for (const auto byte : bytes) {
        value.push_back(static_cast<char>(std::to_integer<unsigned char>(byte)));
    }
    return value;
}

PxAwaitable<void> ExerciseAsyncFile(std::shared_ptr<PxAsyncRuntime> runtime, std::filesystem::path path,
                                    std::shared_ptr<std::promise<PxResult<void>>> completion) {
    const auto deadline = std::chrono::steady_clock::now() + 20s;
    auto opened = co_await PxAsyncFile::OpenAsync(runtime, std::move(path),
                                                  {.access = PxFileAccess::kReadWrite,
                                                   .create_if_missing = true,
                                                   .truncate_existing = true},
                                                  deadline);
    if (!opened) {
        completion->set_value(PxResult<void>::Failure(opened.Error()));
        co_return;
    }
    const auto file = opened.TakeValue();
    const auto first = BytesFromString("GammaRay-");
    const auto second = BytesFromString("中文-\xF0\x9F\x9A\x80");
    const auto large_offset = (2ULL * 1024ULL * 1024ULL * 1024ULL) + 7ULL;

    const auto first_write = co_await PxAsyncFile::WriteAtAsync(file, 0, first, deadline);
    if (!first_write) {
        completion->set_value(PxResult<void>::Failure(first_write.Error()));
        co_return;
    }
    const auto second_write = co_await PxAsyncFile::WriteAtAsync(file, first->size(), second, deadline);
    if (!second_write) {
        completion->set_value(PxResult<void>::Failure(second_write.Error()));
        co_return;
    }
    const auto flushed = co_await PxAsyncFile::FlushAsync(file, deadline);
    if (!flushed) {
        completion->set_value(PxResult<void>::Failure(flushed.Error()));
        co_return;
    }

    const auto combined = co_await PxAsyncFile::ReadAtAsync(file, 0, first->size() + second->size(), deadline);
    const auto past_end = co_await PxAsyncFile::ReadAtAsync(file, large_offset, 1, deadline);
    if (!combined || !past_end || StringFromBytes(*combined.Value()) != "GammaRay-中文-\xF0\x9F\x9A\x80" || !past_end.Value()->empty()) {
        completion->set_value(
            PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kIoError, "test.read", "async read returned unexpected data")));
        co_return;
    }

    const auto statistics = file->GetStatistics();
    if (statistics.bytes_written != first->size() + second->size() || statistics.bytes_read != combined.Value()->size()) {
        completion->set_value(
            PxResult<void>::Failure(MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "test.statistics", "file statistics mismatch")));
        co_return;
    }

    const auto closed = co_await PxAsyncFile::CloseAsync(file, deadline);
    const auto closed_again = co_await PxAsyncFile::CloseAsync(file, deadline);
    if (!closed || !closed_again || file->IsOpen()) {
        completion->set_value(PxResult<void>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kProtocolError, "test.close", "file close was not idempotent")));
        co_return;
    }
    completion->set_value(PxResult<void>::Success());
}

TEST(AsyncFileTest, SupportsUnicodePathsRandomAccessAndIdempotentClose) {
    TemporaryFilePath temporary("gammaray-异步文件-\xF0\x9F\x9A\x80.bin");
    ASSERT_FALSE(temporary.Get().empty());
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2, .blocking_threads = 2, .max_pending_blocking_tasks = 16});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<void>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("async-file-test", [runtime, path = temporary.Get(), completion]() mutable {
        return ExerciseAsyncFile(runtime, std::move(path), completion);
    }));
    ASSERT_EQ(future.wait_for(30s), std::future_status::ready);
    const auto result = future.get();
    if (!result) {
        FAIL() << result.Error().message;
    }
    ASSERT_TRUE(scope->WaitFor(5s));
    runtime->RequestStop();
    runtime->Join();
}

PxAwaitable<void> ExerciseExpiredDeadline(std::shared_ptr<PxAsyncRuntime> runtime, std::filesystem::path path,
                                          std::shared_ptr<std::promise<PxResult<std::shared_ptr<PxAsyncFile>>>> completion) {
    completion->set_value(co_await PxAsyncFile::OpenAsync(runtime, std::move(path), {}, std::chrono::steady_clock::now() - 1ms));
}

TEST(AsyncFileTest, RejectsExpiredOpenDeadline) {
    TemporaryFilePath temporary("gammaray-expired-file.bin");
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1, .blocking_threads = 1, .max_pending_blocking_tasks = 4});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    const auto completion = std::make_shared<std::promise<PxResult<std::shared_ptr<PxAsyncFile>>>>();
    auto future = completion->get_future();

    ASSERT_TRUE(scope->Spawn("async-file-expired", [runtime, path = temporary.Get(), completion]() mutable {
        return ExerciseExpiredDeadline(runtime, std::move(path), completion);
    }));
    ASSERT_EQ(future.wait_for(2s), std::future_status::ready);
    const auto result = future.get();
    ASSERT_FALSE(result);
    EXPECT_EQ(result.Error().code, PxAsyncErrorCode::kTimeout);
    ASSERT_TRUE(scope->WaitFor(2s));
    runtime->RequestStop();
    runtime->Join();
}

} // namespace
} // namespace px
