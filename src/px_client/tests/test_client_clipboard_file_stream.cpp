#include <Windows.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "px_client/modules/clipboard/win/cp_file_stream.h"

namespace px {
namespace {

using namespace std::chrono_literals;

struct RequestState {
    std::mutex mutex;
    std::condition_variable condition;
    int count = 0;
    int64_t index = -1;
    int64_t start = -1;
    ULONG size = 0;
};

struct ReadResult {
    HRESULT result = E_PENDING;
    ULONG bytes_read = 0;
    std::vector<char> bytes;
};

ClipboardFileWrapper MakeFileWrapper() {
    ClipboardFileWrapper wrapper;
    wrapper.file_.set_file_name("remote.bin");
    wrapper.file_.set_full_path("C:/remote/remote.bin");
    wrapper.file_.set_ref_path("remote.bin");
    wrapper.file_.set_total_size(512 * 1024);
    return wrapper;
}

bool WaitForRequests(
    const std::shared_ptr<RequestState>& state,
    int expected_count) {
    std::unique_lock lock(state->mutex);
    return state->condition.wait_for(lock, 2s, [state, expected_count]() {
        return state->count >= expected_count;
    });
}

std::jthread StartRead(
    const Microsoft::WRL::ComPtr<CpFileStream>& stream,
    ULONG request_size,
    const std::shared_ptr<ReadResult>& result,
    const std::shared_ptr<std::promise<void>>& completed) {
    result->bytes.resize(request_size);
    return std::jthread([stream, request_size, result, completed]() {
        result->result = stream->Read(
            result->bytes.data(), request_size, &result->bytes_read);
        completed->set_value();
    });
}

TEST(ClientClipboardFileStream, ReadsMatchingChunksAndIgnoresStaleResponse) {
    const auto requests = std::make_shared<RequestState>();
    const auto lifetime = std::make_shared<std::atomic_bool>(true);
    auto stream = CreateClipboardFileStream(
        [requests](const ClipboardFileWrapper&, int64_t index,
                   int64_t start, ULONG size) {
            {
                std::lock_guard lock(requests->mutex);
                ++requests->count;
                requests->index = index;
                requests->start = start;
                requests->size = size;
            }
            requests->condition.notify_all();
            return true;
        },
        lifetime, MakeFileWrapper());

    const auto first_result = std::make_shared<ReadResult>();
    const auto first_completed = std::make_shared<std::promise<void>>();
    auto first_completed_result = first_completed->get_future();
    auto first_worker = StartRead(
        stream, 256 * 1024, first_result, first_completed);
    ASSERT_TRUE(WaitForRequests(requests, 1));
    {
        std::lock_guard lock(requests->mutex);
        EXPECT_EQ(requests->index, 0);
        EXPECT_EQ(requests->start, 0);
        EXPECT_EQ(requests->size, 128u * 1024u);
    }

    ClipboardRespBuffer stale;
    stale.set_full_name("C:/remote/remote.bin");
    stale.set_req_index(99);
    stale.set_read_size(3);
    stale.set_buffer("old");
    stream->OnClipboardRespBuffer(stale);
    EXPECT_EQ(first_completed_result.wait_for(50ms),
              std::future_status::timeout);

    ClipboardRespBuffer first_response;
    first_response.set_full_name("C:/remote/remote.bin");
    first_response.set_req_index(0);
    first_response.set_read_size(4);
    first_response.set_buffer("data");
    stream->OnClipboardRespBuffer(first_response);
    ASSERT_EQ(first_completed_result.wait_for(2s), std::future_status::ready);
    first_worker.join();
    EXPECT_EQ(first_result->result, S_OK);
    EXPECT_EQ(first_result->bytes_read, 4u);
    EXPECT_EQ(std::string(first_result->bytes.data(), 4), "data");

    const auto second_result = std::make_shared<ReadResult>();
    const auto second_completed = std::make_shared<std::promise<void>>();
    auto second_completed_result = second_completed->get_future();
    auto second_worker = StartRead(
        stream, 16, second_result, second_completed);
    ASSERT_TRUE(WaitForRequests(requests, 2));
    {
        std::lock_guard lock(requests->mutex);
        EXPECT_EQ(requests->index, 1);
        EXPECT_EQ(requests->start, 4);
        EXPECT_EQ(requests->size, 16u);
    }

    ClipboardRespBuffer second_response;
    second_response.set_full_name("C:/remote/remote.bin");
    second_response.set_req_index(1);
    second_response.set_read_size(2);
    second_response.set_buffer("ok");
    stream->OnClipboardRespBuffer(second_response);
    ASSERT_EQ(second_completed_result.wait_for(2s), std::future_status::ready);
    second_worker.join();
    EXPECT_EQ(second_result->result, S_OK);
    EXPECT_EQ(second_result->bytes_read, 2u);
    EXPECT_EQ(std::string(second_result->bytes.data(), 2), "ok");
}

TEST(ClientClipboardFileStream, ExitWakesPendingRead) {
    const auto requests = std::make_shared<RequestState>();
    auto stream = CreateClipboardFileStream(
        [requests](const ClipboardFileWrapper&, int64_t, int64_t, ULONG) {
            {
                std::lock_guard lock(requests->mutex);
                ++requests->count;
            }
            requests->condition.notify_all();
            return true;
        },
        std::make_shared<std::atomic_bool>(true), MakeFileWrapper());

    const auto result = std::make_shared<ReadResult>();
    const auto completed = std::make_shared<std::promise<void>>();
    auto completed_result = completed->get_future();
    auto worker = StartRead(stream, 64, result, completed);
    ASSERT_TRUE(WaitForRequests(requests, 1));
    stream->Exit();
    ASSERT_EQ(completed_result.wait_for(2s), std::future_status::ready);
    worker.join();
    EXPECT_EQ(result->result, S_FALSE);
    EXPECT_EQ(result->bytes_read, 0u);
}

TEST(ClientClipboardFileStream, MatchingResponseCannotBeOverwrittenByUnrelatedData) {
    auto stream = CreateClipboardFileStream(
        [](const ClipboardFileWrapper&, int64_t, int64_t, ULONG) {
            return true;
        },
        std::make_shared<std::atomic_bool>(true), MakeFileWrapper());

    ClipboardRespBuffer matching;
    matching.set_full_name("C:/remote/remote.bin");
    matching.set_req_index(0);
    matching.set_read_size(4);
    matching.set_buffer("good");
    stream->OnClipboardRespBuffer(matching);

    ClipboardRespBuffer stale;
    stale.set_full_name("C:/remote/remote.bin");
    stale.set_req_index(7);
    stale.set_read_size(3);
    stale.set_buffer("bad");
    stream->OnClipboardRespBuffer(stale);

    std::vector<char> output(16);
    ULONG bytes_read = 0;
    EXPECT_EQ(stream->Read(output.data(), output.size(), &bytes_read), S_OK);
    EXPECT_EQ(bytes_read, 4u);
    EXPECT_EQ(std::string(output.data(), bytes_read), "good");
}

TEST(ClientClipboardFileStream, InactiveLifetimeRejectsRead) {
    const auto request_count = std::make_shared<std::atomic_int>(0);
    auto stream = CreateClipboardFileStream(
        [request_count](const ClipboardFileWrapper&, int64_t, int64_t, ULONG) {
            request_count->fetch_add(1);
            return true;
        },
        std::make_shared<std::atomic_bool>(false), MakeFileWrapper());
    std::vector<char> output(16);
    ULONG bytes_read = 123;

    EXPECT_EQ(stream->Read(output.data(), output.size(), &bytes_read), S_FALSE);
    EXPECT_EQ(bytes_read, 0u);
    EXPECT_EQ(request_count->load(), 0);
}

TEST(ClientClipboardFileStream, RejectedRequestReturnsWithoutWaiting) {
    auto stream = CreateClipboardFileStream(
        [](const ClipboardFileWrapper&, int64_t, int64_t, ULONG) {
            return false;
        },
        std::make_shared<std::atomic_bool>(true), MakeFileWrapper());
    std::vector<char> output(16);
    ULONG bytes_read = 123;

    EXPECT_EQ(stream->Read(output.data(), output.size(), &bytes_read), S_FALSE);
    EXPECT_EQ(bytes_read, 0u);
}

}  // namespace
}  // namespace px
