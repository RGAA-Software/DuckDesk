#include <gtest/gtest.h>
#include <spdlog/sinks/base_sink.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "px_common/log.h"

namespace px {
namespace {
using namespace std::chrono_literals;

class BlockingLogSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    bool WaitForWrite() {
        std::unique_lock lock(gate_mutex_);
        return gate_changed_.wait_for(lock, 2s, [&entered = entered_] { return entered; });
    }

    void Release() {
        std::lock_guard lock(gate_mutex_);
        released_ = true;
        gate_changed_.notify_all();
    }

private:
    void sink_it_(const spdlog::details::log_msg&) override {
        std::unique_lock lock(gate_mutex_);
        entered_ = true;
        gate_changed_.notify_all();
        gate_changed_.wait(lock, [&released = released_] { return released; });
    }
    void flush_() override {}

    std::mutex gate_mutex_{};
    std::condition_variable gate_changed_{};
    bool entered_{};
    bool released_{};
};

class MediaLoggingTest : public ::testing::Test {
protected:
    MediaLoggingTest()
        : original_logger_(spdlog::default_logger()),
          log_directory_(std::filesystem::temp_directory_path() /
                         ("pixels-media-log-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        std::filesystem::create_directories(log_directory_);
    }
    ~MediaLoggingTest() override {
        blocking_sink_->Release();
        spdlog::shutdown();
        spdlog::set_default_logger(original_logger_);
        std::error_code removal_error{};
        std::filesystem::remove_all(log_directory_, removal_error);
    }

    std::shared_ptr<spdlog::logger> original_logger_{};
    std::shared_ptr<BlockingLogSink> blocking_sink_{std::make_shared<BlockingLogSink>()};
    std::filesystem::path log_directory_{};
};

TEST_F(MediaLoggingTest, LegacySynchronousWriteBlocksTheCallingMediaThread) {
    ASSERT_TRUE(Logger::InitLog((log_directory_ / "legacy.log").wstring(), true));
    const auto logger = spdlog::default_logger();
    logger->sinks().push_back(blocking_sink_);
    std::atomic_bool returned{};
    std::jthread producer([logger, &returned] {
        logger->info("synchronous media diagnostic");
        returned.store(true);
    });
    const bool entered = blocking_sink_->WaitForWrite();
    std::this_thread::sleep_for(100ms);
    const bool returned_while_blocked = returned.load();
    blocking_sink_->Release();
    producer.join();
    EXPECT_TRUE(entered);
    EXPECT_FALSE(returned_while_blocked);
    EXPECT_TRUE(returned.load());
}

TEST_F(MediaLoggingTest, BlockedFileWorkerAndFullQueueDoNotBlockMediaProducers) {
    ASSERT_TRUE(Logger::InitLog((log_directory_ / "media.log").wstring(), true, true));
    const auto logger = spdlog::default_logger();
    logger->sinks().push_back(blocking_sink_);
    logger->info("start blocked file worker");
    ASSERT_TRUE(blocking_sink_->WaitForWrite());
    const auto started = std::chrono::steady_clock::now();
    for (int diagnostic_index{}; diagnostic_index < 16384; ++diagnostic_index) logger->info("media frame {}", diagnostic_index);
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_LT(elapsed, 500ms);
    EXPECT_GT(spdlog::thread_pool()->overrun_counter(), 0U);
    blocking_sink_->Release();
}

TEST_F(MediaLoggingTest, ShutdownDrainsPendingWritesAndRepeatedInitializationSucceeds) {
    for (int start_index{}; start_index < 3; ++start_index) {
        const auto log_path = log_directory_ / ("restart-" + std::to_string(start_index) + ".log");
        ASSERT_TRUE(Logger::InitLog(log_path.wstring(), true, true));
        const auto logger = spdlog::default_logger();
        for (int diagnostic_index{}; diagnostic_index < 100; ++diagnostic_index) logger->info("queued media frame {}", diagnostic_index);
        spdlog::shutdown();
        std::ifstream log_file(log_path);
        std::string log_line{};
        int persisted_messages{};
        while (std::getline(log_file, log_line)) {
            if (log_line.find("queued media frame") != std::string::npos) ++persisted_messages;
        }
        EXPECT_EQ(persisted_messages, 100);
    }
}
}  // namespace
}  // namespace px
