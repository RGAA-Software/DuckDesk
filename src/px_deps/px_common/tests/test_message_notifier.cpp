#include <gtest/gtest.h>

#include "px_common/message_notifier.h"
#include "px_common/async_runtime.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <vector>

namespace px {
namespace {

using namespace std::chrono_literals;

struct IntMessage {
    int value = 0;
};

struct ProducerMessage {
    int producer = 0;
    int index = 0;
};

struct OuterMessage {};
struct InnerMessage {};
struct OtherMessage {
    int value = 0;
};

TEST(MessageNotifierTest, DispatchesAsynchronouslyOnDedicatedThread) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    std::promise<std::thread::id> callback_thread;
    auto callback_thread_future = callback_thread.get_future();
    const auto sender_thread = std::this_thread::get_id();

    listener->Listen<IntMessage>([&](const IntMessage&) {
        callback_thread.set_value(std::this_thread::get_id());
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(callback_thread_future.wait_for(2s), std::future_status::ready);
    EXPECT_NE(callback_thread_future.get(), sender_thread);
    EXPECT_TRUE(notifier.FlushForTest());
}

TEST(MessageNotifierTest, ListenerCanReenterWithoutDeadlockAndPreservesTailOrder) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    std::mutex mutex;
    std::vector<int> order;

    listener->Listen<OuterMessage>([&](const OuterMessage&) {
        {
            std::lock_guard lock(mutex);
            order.push_back(1);
        }
        EXPECT_TRUE(notifier.PublishAppMessage(InnerMessage{}));
        {
            std::lock_guard lock(mutex);
            order.push_back(2);
        }
    });
    listener->Listen<InnerMessage>([&](const InnerMessage&) {
        std::lock_guard lock(mutex);
        order.push_back(3);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(OuterMessage{}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(order, (std::vector<int>{1, 2, 3}));
}

TEST(MessageNotifierTest, ConcurrentProducersDoNotLoseOrDuplicateMessages) {
    MessageNotifier notifier(65536);
    auto listener = notifier.CreateListener();
    constexpr int kProducerCount = 8;
    constexpr int kMessagesPerProducer = 2000;
    std::mutex mutex;
    std::vector<int> last_index(kProducerCount, -1);
    std::atomic_int received{0};
    std::atomic_int order_errors{0};
    std::atomic_bool post_failed{false};

    listener->Listen<ProducerMessage>([&](const ProducerMessage& message) {
        std::lock_guard lock(mutex);
        if (message.index != last_index[message.producer] + 1) {
            order_errors.fetch_add(1, std::memory_order_relaxed);
        }
        last_index[message.producer] = message.index;
        received.fetch_add(1, std::memory_order_relaxed);
    });

    std::vector<std::thread> producers;
    for (int producer = 0; producer < kProducerCount; ++producer) {
        producers.emplace_back([&, producer]() {
            for (int index = 0; index < kMessagesPerProducer; ++index) {
                if (!notifier.PublishAppMessage(ProducerMessage{producer, index})) {
                    post_failed.store(true, std::memory_order_relaxed);
                    return;
                }
            }
        });
    }
    for (auto& producer : producers) {
        producer.join();
    }

    EXPECT_FALSE(post_failed.load());
    ASSERT_TRUE(notifier.FlushForTest(10s));
    EXPECT_EQ(received.load(), kProducerCount * kMessagesPerProducer);
    EXPECT_EQ(order_errors.load(), 0);
    for (const auto index : last_index) {
        EXPECT_EQ(index, kMessagesPerProducer - 1);
    }
}

TEST(MessageNotifierTest, SlowListenerDoesNotBlockProducer) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();

    listener->Listen<IntMessage>([&](const IntMessage&) {
        entered.set_value();
        release_future.wait();
    });

    const auto begin = std::chrono::steady_clock::now();
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    const auto elapsed = std::chrono::steady_clock::now() - begin;
    EXPECT_LT(elapsed, 100ms);
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    release.set_value();
    EXPECT_TRUE(notifier.FlushForTest());
}

TEST(MessageNotifierTest, ListenerExceptionIsIsolated) {
    MessageNotifier notifier;
    auto throwing_listener = notifier.CreateListener();
    auto healthy_listener = notifier.CreateListener();
    std::atomic_int received{0};

    throwing_listener->Listen<IntMessage>([](const IntMessage&) {
        throw std::runtime_error("expected test exception");
    });
    healthy_listener->Listen<IntMessage>([&](const IntMessage&) {
        received.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(received.load(), 1);
    EXPECT_EQ(notifier.GetStatistics().callback_exceptions, 1u);
}

TEST(MessageNotifierTest, UnlistenPreventsFutureCallbacks) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    std::atomic_int received{0};
    listener->Listen<IntMessage>([&](const IntMessage&) {
        received.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(received.load(), 1);

    listener->UnListenAll();
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{2}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(received.load(), 1);
}

TEST(MessageNotifierTest, QueueLimitRejectsWithoutBlocking) {
    MessageNotifier notifier(2);
    auto listener = notifier.CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::atomic_bool first{true};

    listener->Listen<IntMessage>([&](const IntMessage&) {
        if (first.exchange(false)) {
            entered.set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(notifier.PublishAppMessage(IntMessage{2}));
    EXPECT_TRUE(notifier.PublishAppMessage(IntMessage{3}));
    EXPECT_FALSE(notifier.PublishAppMessage(IntMessage{4}));
    release.set_value();
    EXPECT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(notifier.GetStatistics().rejected, 1u);
}

TEST(MessageNotifierTest, LatestMessageCoalescesAtQueueTail) {
    MessageNotifier notifier(4);
    auto listener = notifier.CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::mutex mutex;
    std::vector<int> values;

    listener->Listen<IntMessage>([&](const IntMessage& message) {
        if (message.value == 0) {
            entered.set_value();
            release_future.wait();
        }
        std::lock_guard lock(mutex);
        values.push_back(message.value);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{0}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(notifier.PublishLatestAppMessage(IntMessage{1}, 7));
    EXPECT_TRUE(notifier.PublishLatestAppMessage(IntMessage{2}, 7));
    EXPECT_TRUE(notifier.PublishLatestAppMessage(IntMessage{3}, 7));
    release.set_value();
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(values, (std::vector<int>{0, 3}));
    EXPECT_EQ(notifier.GetStatistics().coalesced, 2u);
}

TEST(MessageNotifierTest, StopCanBeRequestedFromListener) {
    auto notifier = std::make_shared<MessageNotifier>();
    std::weak_ptr<MessageNotifier> weak_notifier = notifier;
    auto listener = notifier->CreateListener();
    std::promise<void> returned;
    auto returned_future = returned.get_future();

    listener->Listen<IntMessage>([weak_notifier, &returned](const IntMessage&) {
        if (auto notifier = weak_notifier.lock()) {
            notifier->Stop(MessageBusStopMode::kDrain);
        }
        returned.set_value();
    });

    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{1}));
    EXPECT_EQ(returned_future.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(notifier->PublishAppMessage(IntMessage{2}));
}

TEST(MessageNotifierTest, ExecutorMovesCallbackToOwningEventLoop) {
    MessageNotifier notifier;
    std::mutex mutex;
    std::queue<std::function<void()>> tasks;
    std::atomic_int received{0};
    auto listener = notifier.CreateListener([&](std::function<void()> task) {
        std::lock_guard lock(mutex);
        tasks.push(std::move(task));
    });
    listener->Listen<IntMessage>([&](const IntMessage&) {
        received.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(received.load(), 0);

    std::function<void()> task;
    {
        std::lock_guard lock(mutex);
        ASSERT_EQ(tasks.size(), 1u);
        task = std::move(tasks.front());
        tasks.pop();
    }
    task();
    EXPECT_EQ(received.load(), 1);
}

TEST(MessageNotifierTest, UnlistenCancelsCallbackAlreadyPostedToExecutor) {
    MessageNotifier notifier;
    std::mutex mutex;
    std::queue<std::function<void()>> tasks;
    std::atomic_int received{0};
    auto listener = notifier.CreateListener([&](std::function<void()> task) {
        std::lock_guard lock(mutex);
        tasks.push(std::move(task));
    });
    listener->Listen<IntMessage>([&](const IntMessage&) {
        received.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    listener->UnListenAll();

    std::function<void()> task;
    {
        std::lock_guard lock(mutex);
        ASSERT_EQ(tasks.size(), 1u);
        task = std::move(tasks.front());
        tasks.pop();
    }
    task();
    EXPECT_EQ(received.load(), 0);
}

TEST(MessageNotifierTest, StopCancelsCallbackAlreadyPostedToExecutor) {
    MessageNotifier notifier;
    std::mutex mutex;
    std::queue<std::function<void()>> tasks;
    std::atomic_int received{0};
    auto listener = notifier.CreateListener([&](std::function<void()> task) {
        std::lock_guard lock(mutex);
        tasks.push(std::move(task));
    });
    listener->Listen<IntMessage>([&](const IntMessage&) {
        received.fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    notifier.Stop(MessageBusStopMode::kDrain);

    std::function<void()> task;
    {
        std::lock_guard lock(mutex);
        ASSERT_EQ(tasks.size(), 1u);
        task = std::move(tasks.front());
        tasks.pop();
    }
    task();
    EXPECT_EQ(received.load(), 0);
}

TEST(MessageNotifierTest, DrainStopDeliversMessagesAcceptedBeforeStop) {
    auto notifier = std::make_shared<MessageNotifier>();
    auto listener = notifier->CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::atomic_int received{0};

    listener->Listen<IntMessage>([&](const IntMessage& message) {
        if (message.value == 1) {
            entered.set_value();
            release_future.wait();
        }
        received.fetch_add(1, std::memory_order_relaxed);
    });
    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{2}));

    auto stop_future = std::async(std::launch::async, [notifier]() {
        notifier->Stop(MessageBusStopMode::kDrain);
    });
    EXPECT_EQ(stop_future.wait_for(50ms), std::future_status::timeout);
    release.set_value();
    EXPECT_EQ(stop_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(received.load(), 2);
    EXPECT_FALSE(notifier->PublishAppMessage(IntMessage{3}));
}

TEST(MessageNotifierTest, CancelStopDropsQueuedMessages) {
    auto notifier = std::make_shared<MessageNotifier>();
    auto listener = notifier->CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::promise<void> stopped;
    auto stopped_future = stopped.get_future();
    std::atomic_int received{0};

    listener->Listen<IntMessage>([&, notifier](const IntMessage& message) {
        received.fetch_add(1, std::memory_order_relaxed);
        if (message.value == 1) {
            entered.set_value();
            release_future.wait();
            notifier->Stop(MessageBusStopMode::kCancel);
            stopped.set_value();
        }
    });
    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{2}));

    release.set_value();
    EXPECT_EQ(stopped_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(received.load(), 1);
}

TEST(MessageNotifierTest, ConcurrentUnlistenCancelsPendingDelivery) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::promise<void> unlistened;
    auto unlistened_future = unlistened.get_future();
    std::atomic_int received{0};
    std::weak_ptr<MessageListener> weak_listener = listener;

    listener->Listen<IntMessage>([&, weak_listener](const IntMessage& message) {
        received.fetch_add(1, std::memory_order_relaxed);
        if (message.value == 1) {
            entered.set_value();
            release_future.wait();
            if (auto current = weak_listener.lock()) {
                current->UnListenAll();
            }
            unlistened.set_value();
        }
    });
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{2}));

    release.set_value();
    EXPECT_EQ(unlistened_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(received.load(), 1);
}

TEST(MessageNotifierTest, ExecutorExceptionIsIsolated) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener([](std::function<void()>) {
        throw std::runtime_error("expected executor failure");
    });
    listener->Listen<IntMessage>([](const IntMessage&) {});

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(notifier.GetStatistics().callback_exceptions, 1u);
}

TEST(MessageNotifierTest, ListenerCanBeInstalledDuringDispatch) {
    MessageNotifier notifier;
    auto outer = notifier.CreateListener();
    std::shared_ptr<MessageListener> inner;
    std::atomic_int received{0};

    outer->Listen<OuterMessage>([&](const OuterMessage&) {
        inner = notifier.CreateListener();
        inner->Listen<InnerMessage>([&](const InnerMessage&) {
            received.fetch_add(1, std::memory_order_relaxed);
        });
        EXPECT_TRUE(notifier.PublishAppMessage(InnerMessage{}));
    });

    ASSERT_TRUE(notifier.PublishAppMessage(OuterMessage{}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(received.load(), 1);
}

TEST(MessageNotifierTest, CoalescingKeepsIndependentTypeAndKeySlots) {
    MessageNotifier notifier(8);
    auto listener = notifier.CreateListener();
    std::promise<void> entered;
    auto entered_future = entered.get_future();
    std::promise<void> release;
    auto release_future = release.get_future().share();
    std::mutex mutex;
    std::vector<int> values;

    listener->Listen<IntMessage>([&](const IntMessage& message) {
        if (message.value == 0) {
            entered.set_value();
            release_future.wait();
        }
        std::lock_guard lock(mutex);
        values.push_back(message.value);
    });
    listener->Listen<OtherMessage>([&](const OtherMessage& message) {
        std::lock_guard lock(mutex);
        values.push_back(100 + message.value);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{0}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(notifier.PublishLatestAppMessage(IntMessage{1}, 1));
    EXPECT_TRUE(notifier.PublishLatestAppMessage(IntMessage{2}, 2));
    EXPECT_TRUE(notifier.PublishLatestAppMessage(IntMessage{3}, 1));
    EXPECT_TRUE(notifier.PublishLatestAppMessage(OtherMessage{4}, 1));
    EXPECT_TRUE(notifier.PublishLatestAppMessage(OtherMessage{5}, 1));
    release.set_value();

    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(values, (std::vector<int>{0, 2, 3, 105}));
    EXPECT_EQ(notifier.GetStatistics().coalesced, 2u);
}

TEST(MessageNotifierTest, RepeatedConstructionDispatchAndDestructionIsStable) {
    constexpr int kRoutineIterations = 10;
    for (int iteration = 0; iteration < kRoutineIterations; ++iteration) {
        auto notifier = std::make_shared<MessageNotifier>();
        auto listener = notifier->CreateListener();
        std::atomic_int received{0};
        listener->Listen<IntMessage>([&](const IntMessage&) {
            received.fetch_add(1, std::memory_order_relaxed);
        });
        ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{iteration}));
        ASSERT_TRUE(notifier->FlushForTest());
        EXPECT_EQ(received.load(), 1);
    }
}

TEST(MessageNotifierTest, StateLanePreservesPublishOrder) {
    MessageNotifier notifier(MessageNotifierOptions{
        .max_pending_messages = 1024,
        .max_state_callbacks = 1024,
        .max_worker_callbacks = 16,
        .worker_threads = 2});
    auto listener = notifier.CreateListener(MessageExecutionLane::kState);
    auto values = std::make_shared<std::vector<int>>();
    auto values_mutex = std::make_shared<std::mutex>();
    listener->Listen<IntMessage>([values, values_mutex](const IntMessage& message) {
        std::lock_guard lock(*values_mutex);
        values->push_back(message.value);
    });

    for (int value = 0; value < 200; ++value) {
        ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{value}));
    }
    ASSERT_TRUE(notifier.FlushForTest());

    ASSERT_EQ(values->size(), 200u);
    for (int value = 0; value < 200; ++value) {
        EXPECT_EQ((*values)[value], value);
    }
    const auto statistics = notifier.GetStatistics();
    EXPECT_EQ(statistics.state_lane.scheduled, 200u);
    EXPECT_EQ(statistics.state_lane.completed, 200u);
    EXPECT_EQ(statistics.state_lane.pending, 0u);
}

TEST(MessageNotifierTest, SlowWorkerLaneDoesNotBlockControlLane) {
    MessageNotifier notifier(MessageNotifierOptions{
        .max_pending_messages = 32,
        .max_state_callbacks = 16,
        .max_worker_callbacks = 16,
        .worker_threads = 1});
    auto worker_listener = notifier.CreateListener(MessageExecutionLane::kWorker);
    auto control_listener = notifier.CreateListener(MessageExecutionLane::kControl);
    auto worker_entered = std::make_shared<std::promise<void>>();
    auto worker_entered_future = worker_entered->get_future();
    auto release_worker = std::make_shared<std::promise<void>>();
    auto release_worker_future = release_worker->get_future().share();
    auto control_called = std::make_shared<std::promise<void>>();
    auto control_called_future = control_called->get_future();

    worker_listener->Listen<OuterMessage>(
        [worker_entered, release_worker_future](const OuterMessage&) {
            worker_entered->set_value();
            release_worker_future.wait();
        });
    control_listener->Listen<IntMessage>([control_called](const IntMessage&) {
        control_called->set_value();
    });

    ASSERT_TRUE(notifier.PublishAppMessage(OuterMessage{}));
    ASSERT_EQ(worker_entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    EXPECT_EQ(control_called_future.wait_for(500ms), std::future_status::ready);
    release_worker->set_value();
    EXPECT_TRUE(notifier.FlushForTest());
}

TEST(MessageNotifierTest, WorkerLaneRunsIndependentCallbacksInParallel) {
    MessageNotifier notifier(MessageNotifierOptions{
        .max_pending_messages = 32,
        .max_state_callbacks = 16,
        .max_worker_callbacks = 16,
        .worker_threads = 2});
    auto listener = notifier.CreateListener(MessageExecutionLane::kWorker);
    auto entered = std::make_shared<std::atomic_int>(0);
    auto mutex = std::make_shared<std::mutex>();
    auto condition = std::make_shared<std::condition_variable>();
    auto release = std::make_shared<std::atomic_bool>(false);

    listener->Listen<IntMessage>([entered, mutex, condition, release](const IntMessage&) {
        entered->fetch_add(1, std::memory_order_release);
        condition->notify_all();
        std::unique_lock lock(*mutex);
        condition->wait(lock, [release]() {
            return release->load(std::memory_order_acquire);
        });
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{2}));
    {
        std::unique_lock lock(*mutex);
        ASSERT_TRUE(condition->wait_for(lock, 2s, [entered]() {
            return entered->load(std::memory_order_acquire) == 2;
        }));
    }
    release->store(true, std::memory_order_release);
    condition->notify_all();
    EXPECT_TRUE(notifier.FlushForTest());
}

TEST(MessageNotifierTest, WorkerLaneRejectsCallbacksAtItsOwnBound) {
    MessageNotifier notifier(MessageNotifierOptions{
        .max_pending_messages = 32,
        .max_state_callbacks = 16,
        .max_worker_callbacks = 1,
        .worker_threads = 1});
    auto listener = notifier.CreateListener(MessageExecutionLane::kWorker);
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto release_future = release->get_future().share();
    auto calls = std::make_shared<std::atomic_int>(0);

    listener->Listen<IntMessage>([entered, release_future, calls](const IntMessage&) {
        if (calls->fetch_add(1, std::memory_order_relaxed) == 0) {
            entered->set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{2}));

    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (notifier.GetStatistics().worker_lane.rejected == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(notifier.GetStatistics().worker_lane.rejected, 1u);
    release->set_value();
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(calls->load(std::memory_order_relaxed), 1);
}

TEST(MessageNotifierTest, UnlistenCancelsWorkerCallbackAlreadyQueued) {
    MessageNotifier notifier(MessageNotifierOptions{
        .max_pending_messages = 32,
        .max_state_callbacks = 16,
        .max_worker_callbacks = 16,
        .worker_threads = 1});
    auto listener = notifier.CreateListener(MessageExecutionLane::kWorker);
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto release_future = release->get_future().share();
    auto calls = std::make_shared<std::atomic_int>(0);

    listener->Listen<IntMessage>([entered, release_future, calls](const IntMessage& message) {
        calls->fetch_add(1, std::memory_order_relaxed);
        if (message.value == 1) {
            entered->set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{2}));
    listener->UnListenAll();
    release->set_value();
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(calls->load(std::memory_order_relaxed), 1);
}

TEST(MessageNotifierTest, DrainStopCanBeRequestedFromWorkerLane) {
    auto notifier = std::make_shared<MessageNotifier>();
    std::weak_ptr<MessageNotifier> weak_notifier = notifier;
    auto listener = notifier->CreateListener(MessageExecutionLane::kWorker);
    auto stop_returned = std::make_shared<std::promise<void>>();
    auto stop_returned_future = stop_returned->get_future();

    listener->Listen<IntMessage>([weak_notifier, stop_returned](const IntMessage&) {
        if (auto owner = weak_notifier.lock()) {
            owner->Stop(MessageBusStopMode::kDrain);
        }
        stop_returned->set_value();
    });

    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{1}));
    EXPECT_EQ(stop_returned_future.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(notifier->PublishAppMessage(IntMessage{2}));
}

TEST(MessageNotifierTest, UiLaneWithoutExecutorIsRejected) {
    MessageNotifier notifier;
    auto listener = notifier.CreateListener(MessageExecutionLane::kUi);
    auto calls = std::make_shared<std::atomic_int>(0);
    listener->Listen<IntMessage>([calls](const IntMessage&) {
        calls->fetch_add(1, std::memory_order_relaxed);
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(calls->load(std::memory_order_relaxed), 0);
    EXPECT_EQ(notifier.GetStatistics().ui_lane.rejected, 1u);
}

TEST(MessageNotifierTest, SlowStateLaneDoesNotBlockControlLane) {
    MessageNotifier notifier;
    auto state_listener = notifier.CreateListener(MessageExecutionLane::kState);
    auto control_listener = notifier.CreateListener(MessageExecutionLane::kControl);
    auto state_entered = std::make_shared<std::promise<void>>();
    auto state_entered_future = state_entered->get_future();
    auto release_state = std::make_shared<std::promise<void>>();
    auto release_state_future = release_state->get_future().share();
    auto control_called = std::make_shared<std::promise<void>>();
    auto control_called_future = control_called->get_future();

    state_listener->Listen<OuterMessage>(
        [state_entered, release_state_future](const OuterMessage&) {
            state_entered->set_value();
            release_state_future.wait();
        });
    control_listener->Listen<IntMessage>([control_called](const IntMessage&) {
        control_called->set_value();
    });

    ASSERT_TRUE(notifier.PublishAppMessage(OuterMessage{}));
    ASSERT_EQ(state_entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    EXPECT_EQ(control_called_future.wait_for(500ms), std::future_status::ready);
    release_state->set_value();
    EXPECT_TRUE(notifier.FlushForTest());
}

TEST(MessageNotifierTest, StateLaneRejectsCallbacksAtItsOwnBound) {
    MessageNotifier notifier(MessageNotifierOptions{
        .max_pending_messages = 32,
        .max_state_callbacks = 1,
        .max_worker_callbacks = 16,
        .worker_threads = 1});
    auto listener = notifier.CreateListener(MessageExecutionLane::kState);
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto release_future = release->get_future().share();
    auto calls = std::make_shared<std::atomic_int>(0);

    listener->Listen<IntMessage>([entered, release_future, calls](const IntMessage&) {
        if (calls->fetch_add(1, std::memory_order_relaxed) == 0) {
            entered->set_value();
            release_future.wait();
        }
    });

    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);
    ASSERT_TRUE(notifier.PublishAppMessage(IntMessage{2}));
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (notifier.GetStatistics().state_lane.rejected == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    EXPECT_EQ(notifier.GetStatistics().state_lane.rejected, 1u);
    release->set_value();
    ASSERT_TRUE(notifier.FlushForTest());
    EXPECT_EQ(calls->load(std::memory_order_relaxed), 1);
}

TEST(MessageNotifierTest, WorkerCallbackCanInstallListenerAndPublish) {
    auto notifier = std::make_shared<MessageNotifier>();
    std::weak_ptr<MessageNotifier> weak_notifier = notifier;
    auto outer_listener = notifier->CreateListener(MessageExecutionLane::kWorker);
    auto inner_listener = std::make_shared<std::shared_ptr<MessageListener>>();
    auto inner_called = std::make_shared<std::promise<void>>();
    auto inner_called_future = inner_called->get_future();

    outer_listener->Listen<OuterMessage>(
        [weak_notifier, inner_listener, inner_called](const OuterMessage&) {
            if (auto owner = weak_notifier.lock()) {
                *inner_listener = owner->CreateListener(MessageExecutionLane::kControl);
                (*inner_listener)->Listen<InnerMessage>(
                    [inner_called](const InnerMessage&) { inner_called->set_value(); });
                owner->PublishAppMessage(InnerMessage{});
            }
        });

    ASSERT_TRUE(notifier->PublishAppMessage(OuterMessage{}));
    EXPECT_EQ(inner_called_future.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(notifier->FlushForTest());
}

TEST(MessageNotifierTest, ExternalDrainStopWaitsForWorkerLane) {
    auto notifier = std::make_shared<MessageNotifier>();
    auto listener = notifier->CreateListener(MessageExecutionLane::kWorker);
    auto entered = std::make_shared<std::promise<void>>();
    auto entered_future = entered->get_future();
    auto release = std::make_shared<std::promise<void>>();
    auto release_future = release->get_future().share();

    listener->Listen<IntMessage>([entered, release_future](const IntMessage&) {
        entered->set_value();
        release_future.wait();
    });
    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{1}));
    ASSERT_EQ(entered_future.wait_for(2s), std::future_status::ready);

    auto stop = std::async(std::launch::async, [notifier]() {
        notifier->Stop(MessageBusStopMode::kDrain);
    });
    EXPECT_EQ(stop.wait_for(50ms), std::future_status::timeout);
    release->set_value();
    EXPECT_EQ(stop.wait_for(2s), std::future_status::ready);
}

TEST(MessageNotifierTest, CancelFromWorkerDropsQueuedWorkerCallbacks) {
    auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{
        .max_pending_messages = 32,
        .max_state_callbacks = 16,
        .max_worker_callbacks = 16,
        .worker_threads = 1});
    std::weak_ptr<MessageNotifier> weak_notifier = notifier;
    auto listener = notifier->CreateListener(MessageExecutionLane::kWorker);
    auto calls = std::make_shared<std::atomic_int>(0);
    auto stopped = std::make_shared<std::promise<void>>();
    auto stopped_future = stopped->get_future();

    listener->Listen<IntMessage>([weak_notifier, calls, stopped](const IntMessage&) {
        if (calls->fetch_add(1, std::memory_order_relaxed) == 0) {
            if (auto owner = weak_notifier.lock()) {
                owner->Stop(MessageBusStopMode::kCancel);
            }
            stopped->set_value();
        }
    });

    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{1}));
    ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{2}));
    ASSERT_EQ(stopped_future.wait_for(2s), std::future_status::ready);
    EXPECT_EQ(calls->load(std::memory_order_relaxed), 1);
}

TEST(MessageNotifierTest, ExternalRuntimeRemainsUsableAfterNotifierStops) {
    for (int round = 0; round < 10; ++round) {
        const auto runtime = PxAsyncRuntime::Create({.worker_threads = 2});
        ASSERT_TRUE(runtime->Start());
        auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{
            .worker_threads = 2,
            .runtime = runtime,
        });
        auto listener = notifier->CreateListener(MessageExecutionLane::kState);
        auto delivered = std::make_shared<std::promise<void>>();
        auto delivered_future = delivered->get_future();
        listener->Listen<IntMessage>([delivered](const IntMessage&) {
            delivered->set_value();
        });

        ASSERT_TRUE(notifier->PublishAppMessage(IntMessage{round}));
        ASSERT_EQ(delivered_future.wait_for(2s), std::future_status::ready)
            << "round=" << round;
        notifier->Stop(MessageBusStopMode::kDrain);

        auto runtime_alive = std::make_shared<std::promise<void>>();
        auto runtime_alive_future = runtime_alive->get_future();
        asio::post(runtime->Executor(PxAsyncLane::kState), [runtime_alive]() {
            runtime_alive->set_value();
        });
        EXPECT_EQ(runtime_alive_future.wait_for(2s), std::future_status::ready)
            << "round=" << round;

        notifier.reset();
        runtime->RequestStop();
        runtime->Join();
    }
}

} // namespace
} // namespace px
