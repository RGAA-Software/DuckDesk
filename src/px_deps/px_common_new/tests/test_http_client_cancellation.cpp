#include <gtest/gtest.h>

#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include <asio2/external/asio.hpp>

#include "px_common_new/http_client.h"
#include "px_common_new/task_runtime.h"

namespace px {
namespace {

using namespace std::chrono_literals;

class TemporaryUploadFile final {
public:
    TemporaryUploadFile() {
        path_ = std::filesystem::temp_directory_path()
            / std::format("px_http_cancel_{}.bin",
                          std::chrono::steady_clock::now().time_since_epoch().count());
        {
            std::ofstream stream(path_, std::ios::binary);
            stream.put('\0');
        }
        std::error_code error;
        std::filesystem::resize_file(path_, 32ULL * 1024ULL * 1024ULL, error);
        if (error) {
            throw std::runtime_error(error.message());
        }
    }

    ~TemporaryUploadFile() {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }

    [[nodiscard]] const std::filesystem::path& Path() const {
        return path_;
    }

private:
    std::filesystem::path path_;
};

class SlowUploadServer final {
public:
    SlowUploadServer()
        : io_(std::make_shared<asio::io_context>()),
          acceptor_(std::make_shared<asio::ip::tcp::acceptor>(
              *io_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 0))),
          socket_(std::make_shared<asio::ip::tcp::socket>(*io_)),
          accepted_(std::make_shared<std::atomic_bool>(false)) {}

    ~SlowUploadServer() {
        Stop();
    }

    SlowUploadServer(const SlowUploadServer&) = delete;
    SlowUploadServer& operator=(const SlowUploadServer&) = delete;

    void Start() {
        const auto acceptor = acceptor_;
        const auto socket = socket_;
        const auto accepted = accepted_;
        worker_ = std::thread([acceptor, socket, accepted]() {
            asio::error_code error;
            acceptor->accept(*socket, error);
            if (error) {
                return;
            }
            accepted->store(true, std::memory_order_release);
            std::array<char, 4096> buffer{};
            while (!error) {
                static_cast<void>(socket->read_some(asio::buffer(buffer), error));
                if (!error) {
                    std::this_thread::sleep_for(10ms);
                }
            }
        });
    }

    void Stop() {
        asio::error_code ignored;
        socket_->close(ignored);
        acceptor_->close(ignored);
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    [[nodiscard]] std::uint16_t Port() const {
        return acceptor_->local_endpoint().port();
    }

    [[nodiscard]] bool WaitUntilAccepted(std::chrono::milliseconds timeout) const {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline) {
            if (accepted_->load(std::memory_order_acquire)) {
                return true;
            }
            std::this_thread::sleep_for(5ms);
        }
        return accepted_->load(std::memory_order_acquire);
    }

private:
    std::shared_ptr<asio::io_context> io_;
    std::shared_ptr<asio::ip::tcp::acceptor> acceptor_;
    std::shared_ptr<asio::ip::tcp::socket> socket_;
    std::shared_ptr<std::atomic_bool> accepted_;
    std::thread worker_;
};

TEST(HttpClientCancellation, CancelsMultipartUploadAlreadyInProgress) {
    TemporaryUploadFile file;
    SlowUploadServer server;
    server.Start();

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto response = std::make_shared<HttpResponse>();
    const auto client = HttpClient::Make("127.0.0.1", server.Port(), "/upload", 10000);
    client->SetCancellationSignal(cancellation);
    const auto path = file.Path().string();
    const auto started_at = std::chrono::steady_clock::now();
    std::thread request([client, cancellation, response, path]() {
        *response = client->PostMultiPart(
            {{"token", "test"}}, {}, {{"file", path}});
    });

    const bool accepted = server.WaitUntilAccepted(2s);
    std::this_thread::sleep_for(100ms);
    cancellation->store(true, std::memory_order_release);
    request.join();
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    server.Stop();

    ASSERT_TRUE(accepted);
    EXPECT_LT(elapsed, 3s);
    EXPECT_NE(response->status, 200);
}

TEST(HttpClientCancellation, PreCancelledMultipartDoesNotWaitForTimeout) {
    TemporaryUploadFile file;
    SlowUploadServer server;
    server.Start();
    const auto cancellation = std::make_shared<std::atomic_bool>(true);
    const auto client = HttpClient::Make("127.0.0.1", server.Port(), "/upload", 10000);
    client->SetCancellationSignal(cancellation);
    const auto started_at = std::chrono::steady_clock::now();
    const auto response = client->PostMultiPart(
        {{"token", "test"}}, {}, {{"file", file.Path().string()}});
    const auto elapsed = std::chrono::steady_clock::now() - started_at;
    server.Stop();

    EXPECT_LT(elapsed, 1s);
    EXPECT_NE(response.status, 200);
}

TEST(HttpClientCancellation, ManagedBlockingWorkerJoinsAfterCancellation) {
    TemporaryUploadFile file;
    SlowUploadServer server;
    server.Start();

    const auto cancellation = std::make_shared<std::atomic_bool>(false);
    const auto completed = std::make_shared<std::atomic_bool>(false);
    const auto client = HttpClient::Make("127.0.0.1", server.Port(), "/upload", 10000);
    client->SetCancellationSignal(cancellation);
    const auto path = file.Path().string();
    const auto runtime = std::make_shared<TaskRuntime>(1);
    const auto task_id = runtime->Post(SimpleThreadTask::Make(
        [client, cancellation, completed, path]() {
            static_cast<void>(client->PostMultiPart(
                {{"token", "test"}}, {}, {{"file", path}}));
            completed->store(true, std::memory_order_release);
        }));
    ASSERT_NE(task_id, 0u);

    const bool accepted = server.WaitUntilAccepted(2s);
    const auto stop_started_at = std::chrono::steady_clock::now();
    cancellation->store(true, std::memory_order_release);
    runtime->Exit();
    const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started_at;
    server.Stop();

    ASSERT_TRUE(accepted);
    EXPECT_TRUE(completed->load(std::memory_order_acquire));
    EXPECT_LT(stop_elapsed, 3s);
}

} // namespace
} // namespace px
