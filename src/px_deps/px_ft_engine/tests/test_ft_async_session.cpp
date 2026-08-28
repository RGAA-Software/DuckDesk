#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <thread>

#include "ft_async_session.h"

namespace px::ft {
namespace {

using namespace std::chrono_literals;

struct AsyncSendProbe {
    std::atomic_int attempts{0};
    std::atomic_int accepted{0};
    std::shared_ptr<std::promise<void>> accepted_signal =
        std::make_shared<std::promise<void>>();
};

TEST(FtAsyncSession, BusyRetriesThenCommitsExactlyOnce) {
    const auto probe = std::make_shared<AsyncSendProbe>();
    auto accepted_future = probe->accepted_signal->get_future();
    const auto session = FtAsyncSession::Create([probe](const auto& message) {
        EXPECT_TRUE(message);
        const auto attempt = ++probe->attempts;
        if (attempt <= 10) {
            return FileTransferSendResult::Busy("test backpressure");
        }
        if (++probe->accepted == 1) {
            probe->accepted_signal->set_value();
        }
        return FileTransferSendResult::Accepted();
    });

    ASSERT_TRUE(session->Start());
    ASSERT_TRUE(session->Post("receive", [](const std::shared_ptr<FtEngine>& engine) {
        engine->ReceiveFiles("remote.bin", false, "local.bin");
    }));
    ASSERT_EQ(accepted_future.wait_for(2s), std::future_status::ready);

    // Allow another pump turn; a committed message must never reappear.
    std::this_thread::sleep_for(20ms);
    EXPECT_EQ(probe->accepted.load(), 1);
    EXPECT_EQ(probe->attempts.load(), 11);
    const auto statistics = session->GetStatistics();
    EXPECT_EQ(statistics.accepted_messages, 1U);
    EXPECT_EQ(statistics.busy_retries, 10U);
    EXPECT_TRUE(session->StopAndWait(2s));
}

TEST(FtAsyncSession, StopConvergesWhileTransportIsDisconnected) {
    const auto attempts = std::make_shared<std::atomic_int>(0);
    const auto session = FtAsyncSession::Create([attempts](const auto&) {
        ++*attempts;
        return FileTransferSendResult::Disconnected("offline");
    });
    ASSERT_TRUE(session->Start());
    ASSERT_TRUE(session->Post("receive", [](const std::shared_ptr<FtEngine>& engine) {
        engine->ReceiveFiles("remote.bin", false, "local.bin");
    }));

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (attempts->load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_GT(attempts->load(), 0);
    EXPECT_TRUE(session->StopAndWait(2s));
    EXPECT_FALSE(session->HasJobs());
}

TEST(FtAsyncSession, RepeatedStartStopTenRounds) {
    for (int round = 0; round < 10; ++round) {
        const auto session = FtAsyncSession::Create([](const auto&) {
            return FileTransferSendResult::Accepted();
        });
        ASSERT_TRUE(session->Start()) << "round=" << round;
        ASSERT_TRUE(session->Post("noop", [](const std::shared_ptr<FtEngine>&) {}));
        ASSERT_TRUE(session->StopAndWait(2s)) << "round=" << round;
    }
}

TEST(FtAsyncSession, SharedRuntimeKeepsOtherSessionAliveAfterOneStops) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto first_engine = std::make_shared<FtEngine>();
    const auto second_engine = std::make_shared<FtEngine>();
    const auto first_count = std::make_shared<std::atomic_int>(0);
    const auto second_count = std::make_shared<std::atomic_int>(0);
    const auto first = FtAsyncSession::CreateOnRuntime(
        runtime, first_engine, [first_count](const auto&) {
            ++*first_count;
            return FileTransferSendResult::Accepted();
        });
    const auto second = FtAsyncSession::CreateOnRuntime(
        runtime, second_engine, [second_count](const auto&) {
            ++*second_count;
            return FileTransferSendResult::Accepted();
        });

    ASSERT_TRUE(first->Start());
    ASSERT_TRUE(second->Start());
    ASSERT_TRUE(first->Post("first-receive", [](const auto& engine) {
        engine->ReceiveFiles("first.bin", false, "first.out");
    }));
    ASSERT_TRUE(second->Post("second-receive", [](const auto& engine) {
        engine->ReceiveFiles("second.bin", false, "second.out");
    }));

    const auto first_deadline = std::chrono::steady_clock::now() + 2s;
    while (first_count->load() == 0 && std::chrono::steady_clock::now() < first_deadline) {
        std::this_thread::yield();
    }
    ASSERT_GT(first_count->load(), 0);
    ASSERT_TRUE(first->StopAndWait(2s));
    EXPECT_FALSE(runtime->IsStopping());

    const auto before = second_count->load();
    ASSERT_TRUE(second->Post("second-still-alive", [](const auto& engine) {
        engine->ReadDir("/", false);
    }));
    const auto second_deadline = std::chrono::steady_clock::now() + 2s;
    while (second_count->load() == before &&
           std::chrono::steady_clock::now() < second_deadline) {
        std::this_thread::yield();
    }
    EXPECT_GT(second_count->load(), before);
    EXPECT_TRUE(second->StopAndWait(2s));
    runtime->RequestStop();
    runtime->Join();
}

TEST(FtAsyncSession, SharedRuntimeRejectsNullDependencies) {
    const auto runtime = PxAsyncRuntime::Create();
    const auto engine = std::make_shared<FtEngine>();
    const auto sender = [](const auto&) {
        return FileTransferSendResult::Accepted();
    };
    EXPECT_THROW(FtAsyncSession::CreateOnRuntime({}, engine, sender),
                 std::invalid_argument);
    EXPECT_THROW(FtAsyncSession::CreateOnRuntime(runtime, {}, sender),
                 std::invalid_argument);
}

TEST(FtAsyncSession, PostAndWaitRunsFinalizerBeforeSharedSessionStops) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
    ASSERT_TRUE(runtime->Start());
    const auto engine = std::make_shared<FtEngine>();
    const auto finalized = std::make_shared<std::atomic_bool>(false);
    const auto session = FtAsyncSession::CreateOnRuntime(
        runtime, engine,
        [](const auto&) { return FileTransferSendResult::Accepted(); },
        {}, PxAsyncLane::kState);
    ASSERT_TRUE(session->Start());
    ASSERT_TRUE(session->PostAndWait(
        "finalize",
        [finalized](const auto&) { finalized->store(true); }, 2s));
    EXPECT_TRUE(finalized->load());
    EXPECT_TRUE(session->StopAndWait(2s));
    EXPECT_FALSE(runtime->IsStopping());
    runtime->RequestStop();
    runtime->Join();
}

TEST(FtAsyncSession, PostAndWaitReportsCommandFailure) {
    const auto session = FtAsyncSession::Create(
        [](const auto&) { return FileTransferSendResult::Accepted(); });
    ASSERT_TRUE(session->Start());
    EXPECT_FALSE(session->PostAndWait(
        "throws", [](const auto&) { throw std::runtime_error("test"); }, 2s));
    EXPECT_TRUE(session->StopAndWait(2s));
}

} // namespace
} // namespace px::ft
