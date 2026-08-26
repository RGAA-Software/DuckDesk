#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include <asio2/util/event_dispatcher.hpp>

namespace
{
    using Dispatcher = asio2::event_dispatcher<int, void()>;
    using namespace std::chrono_literals;

    struct SnapshotGate {
        std::mutex mutex;
        std::condition_variable condition;
        bool first_entered = false;
        bool release_first = false;
        std::atomic_int second_calls{0};
    };
}

TEST(AsioEventDispatcherConcurrency, CallbackCanRemoveItselfWithoutDeadlock)
{
    auto dispatcher = std::make_shared<Dispatcher>();
    std::weak_ptr<Dispatcher> weak_dispatcher = dispatcher;
    auto self_handle = std::make_shared<Dispatcher::listener_type>();
    auto calls = std::make_shared<std::atomic_int>(0);

    *self_handle = dispatcher->append_listener(1, [weak_dispatcher, self_handle, calls]() {
        calls->fetch_add(1, std::memory_order_relaxed);
        if (auto owner = weak_dispatcher.lock()) {
            owner->remove_listener(*self_handle);
        }
    });

    auto dispatch = std::async(std::launch::async, [dispatcher]() {
        dispatcher->direct_dispatch(1);
        dispatcher->direct_dispatch(1);
    });

    ASSERT_EQ(dispatch.wait_for(2s), std::future_status::ready);
    dispatch.get();
    EXPECT_EQ(calls->load(std::memory_order_relaxed), 1);
    EXPECT_FALSE(dispatcher->has_any_listener(1));
}

TEST(AsioEventDispatcherConcurrency, CallbackCanClearDispatcherWithoutDeadlock)
{
    auto dispatcher = std::make_shared<Dispatcher>();
    std::weak_ptr<Dispatcher> weak_dispatcher = dispatcher;
    auto first_calls = std::make_shared<std::atomic_int>(0);
    auto second_calls = std::make_shared<std::atomic_int>(0);

    dispatcher->append_listener(1, [weak_dispatcher, first_calls]() {
        first_calls->fetch_add(1, std::memory_order_relaxed);
        if (auto owner = weak_dispatcher.lock()) {
            owner->clear_all_listeners();
        }
    });
    dispatcher->append_listener(1, [second_calls]() {
        second_calls->fetch_add(1, std::memory_order_relaxed);
    });

    auto dispatch = std::async(std::launch::async, [dispatcher]() {
        dispatcher->direct_dispatch(1);
        dispatcher->direct_dispatch(1);
    });

    ASSERT_EQ(dispatch.wait_for(2s), std::future_status::ready);
    dispatch.get();
    EXPECT_EQ(first_calls->load(std::memory_order_relaxed), 1);
    EXPECT_EQ(second_calls->load(std::memory_order_relaxed), 1);
    EXPECT_EQ(dispatcher->get_listener_count(), 0u);
}

TEST(AsioEventDispatcherConcurrency, RemovalAfterSnapshotAllowsOnlyCurrentDispatch)
{
    auto dispatcher = std::make_shared<Dispatcher>();
    auto gate = std::make_shared<SnapshotGate>();

    dispatcher->append_listener(1, [gate]() {
        std::unique_lock lock(gate->mutex);
        gate->first_entered = true;
        gate->condition.notify_all();
        gate->condition.wait(lock, [gate]() { return gate->release_first; });
    });
    const auto second_handle = dispatcher->append_listener(1, [gate]() {
        gate->second_calls.fetch_add(1, std::memory_order_relaxed);
    });

    auto dispatch = std::async(std::launch::async, [dispatcher]() {
        dispatcher->direct_dispatch(1);
    });

    {
        std::unique_lock lock(gate->mutex);
        ASSERT_TRUE(gate->condition.wait_for(lock, 2s, [gate]() {
            return gate->first_entered;
        }));
    }
    ASSERT_TRUE(dispatcher->remove_listener(second_handle));
    {
        std::lock_guard lock(gate->mutex);
        gate->release_first = true;
    }
    gate->condition.notify_all();

    ASSERT_EQ(dispatch.wait_for(2s), std::future_status::ready);
    dispatch.get();
    EXPECT_EQ(gate->second_calls.load(std::memory_order_relaxed), 1);

    dispatcher->direct_dispatch(1);
    EXPECT_EQ(gate->second_calls.load(std::memory_order_relaxed), 1);
}

TEST(AsioEventDispatcherConcurrency, ConcurrentMutationAndDispatchRemainSafe)
{
    constexpr int kIterations = 3000;
    auto dispatcher = std::make_shared<Dispatcher>();
    auto callback_calls = std::make_shared<std::atomic_uint64_t>(0);
    auto start = std::make_shared<std::atomic_bool>(false);

    auto dispatch_task = std::async(std::launch::async,
        [dispatcher, start]() {
            while (!start->load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < kIterations; ++i) {
                dispatcher->direct_dispatch(i % 4);
            }
        });

    auto mutation_task = std::async(std::launch::async,
        [dispatcher, callback_calls, start]() {
            while (!start->load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            std::vector<Dispatcher::listener_type> handles;
            handles.reserve(64);
            for (int i = 0; i < kIterations; ++i) {
                handles.emplace_back(dispatcher->append_listener(i % 4, [callback_calls]() {
                    callback_calls->fetch_add(1, std::memory_order_relaxed);
                }));
                if (handles.size() >= 32) {
                    dispatcher->remove_listener(handles.front());
                    handles.erase(handles.begin());
                }
            }
            for (const auto& handle : handles) {
                dispatcher->remove_listener(handle);
            }
        });

    auto clear_task = std::async(std::launch::async,
        [dispatcher, start]() {
            while (!start->load(std::memory_order_acquire)) {
                std::this_thread::yield();
            }
            for (int i = 0; i < 100; ++i) {
                static_cast<void>(dispatcher->get_listener_count(i % 4));
                static_cast<void>(dispatcher->has_any_listener(i % 4));
                if ((i % 10) == 0) {
                    dispatcher->clear_all_listeners();
                }
            }
        });

    start->store(true, std::memory_order_release);
    EXPECT_EQ(dispatch_task.wait_for(10s), std::future_status::ready);
    EXPECT_EQ(mutation_task.wait_for(10s), std::future_status::ready);
    EXPECT_EQ(clear_task.wait_for(10s), std::future_status::ready);
    dispatch_task.get();
    mutation_task.get();
    clear_task.get();

    dispatcher->clear_all_listeners();
    EXPECT_EQ(dispatcher->get_listener_count(), 0u);
}

TEST(AsioEventDispatcherConcurrency, SnapshotPreservesPrependAndAppendOrder)
{
    auto dispatcher = std::make_shared<Dispatcher>();
    auto order = std::make_shared<std::vector<int>>();

    dispatcher->append_listener(1, [order]() { order->push_back(2); });
    dispatcher->prepend_listener(1, [order]() { order->push_back(1); });
    dispatcher->append_listener(1, [order]() { order->push_back(3); });

    dispatcher->direct_dispatch(1);
    EXPECT_EQ(*order, (std::vector<int>{1, 2, 3}));
}
