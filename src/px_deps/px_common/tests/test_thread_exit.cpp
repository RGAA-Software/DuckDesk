#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <memory>
#include <stdexcept>

#include "px_common/thread.h"

namespace px
{

    TEST(ThreadExitTest, WorkerMayRequestItsOwnExitWithoutJoiningItself) {
        auto worker = Thread::Make("self-exit-test", 4);
        worker->Poll();

        std::promise<void> returned_from_exit;
        auto returned = returned_from_exit.get_future();
        worker->Post([worker, &returned_from_exit]() {
            worker->Exit();
            returned_from_exit.set_value();
        });

        EXPECT_EQ(returned.wait_for(std::chrono::seconds(2)), std::future_status::ready);

        // Reap the finished std::thread from its owner. This must be safe and
        // idempotent after the worker requested exit from inside its task.
        worker->Exit();
        EXPECT_TRUE(worker->IsExit());
    }

    TEST(ThreadExitTest, LastOwnerMayBeReleasedInsideWorkerTask) {
        auto worker = Thread::Make("last-owner-release", 4);
        std::weak_ptr<Thread> weak_worker = worker;
        auto callback_returned = std::make_shared<std::promise<void>>();
        auto returned = callback_returned->get_future();

        worker->Poll();
        worker->Post([owned_worker = worker, callback_returned]() mutable {
            owned_worker->Exit();
            callback_returned->set_value();
            owned_worker.reset();
        });
        worker.reset();

        ASSERT_EQ(returned.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!weak_worker.expired() && std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }
        EXPECT_TRUE(weak_worker.expired());
    }

    TEST(ThreadExitTest, QueueOverflowCallbackMayReenterThread) {
        auto worker = Thread::Make("overflow-reentry", 1);
        auto callback_returned = std::make_shared<std::promise<void>>();
        auto returned = callback_returned->get_future();
        std::weak_ptr<Thread> weak_worker = worker;

        worker->SetOnFrontTaskCallback([weak_worker, callback_returned](ThreadTaskPtr) {
            if (auto owner = weak_worker.lock()) {
                owner->Clear();
            }
            callback_returned->set_value();
        });
        worker->Post([]() {});
        worker->Post([]() {});

        EXPECT_EQ(returned.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        worker->Exit();
    }

    TEST(ThreadExitTest, RepeatedStartStopIsStable) {
        constexpr int kRoutineIterations = 10;
        for (int iteration = 0; iteration < kRoutineIterations; ++iteration) {
            auto worker = Thread::Make("repeat-start-stop", 4);
            auto completed = std::make_shared<std::promise<void>>();
            auto future = completed->get_future();
            worker->Poll();
            worker->Post([completed]() { completed->set_value(); });
            ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
            worker->Exit();
            EXPECT_TRUE(worker->IsExit());
        }
    }

    TEST(ThreadExitTest, StandardExceptionDoesNotTerminateWorkerOrBlockNextTask) {
        auto worker = Thread::Make("standard-exception", 4);
        auto completed = std::make_shared<std::promise<void>>();
        auto future = completed->get_future();

        worker->Poll();
        worker->Post([]() { throw std::runtime_error("expected test failure"); });
        worker->Post([completed]() { completed->set_value(); });

        ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        worker->Exit();
        EXPECT_EQ(worker->ExecCount(), 2UL);
    }

    TEST(ThreadExitTest, NonStandardExceptionDoesNotTerminateWorkerOrBlockNextTask) {
        auto worker = Thread::Make("non-standard-exception", 4);
        auto completed = std::make_shared<std::promise<void>>();
        auto future = completed->get_future();

        worker->Poll();
        worker->Post([]() { throw 7; });
        worker->Post([completed]() { completed->set_value(); });

        ASSERT_EQ(future.wait_for(std::chrono::seconds(2)), std::future_status::ready);
        worker->Exit();
        EXPECT_EQ(worker->ExecCount(), 2UL);
    }

    TEST(ThreadExitTest, OnceTaskExceptionIsContainedAndCompletionIsReported) {
        auto worker = Thread::MakeOnceTask(
            []() { throw std::runtime_error("expected once-task failure"); },
            "once-exception", true);

        EXPECT_TRUE(worker->IsLastTaskReturned());
        EXPECT_TRUE(worker->IsExit());
        worker->Exit();
    }

}
