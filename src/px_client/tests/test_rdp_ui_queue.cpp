#include "rdp/rdp_ui_queue.h"
#include <QCoreApplication>
#include <QEvent>
#include <gtest/gtest.h>
#include <limits>
#include <thread>

namespace px::rdp {
namespace {
void Dispatch() {
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
}

TEST(RdpUiQueue, CloseDiscardsQueuedActionsAndRejectsFurtherPosts) {
    auto receiver = std::make_unique<QObject>();
    auto calls = std::make_shared<int>(0);
    auto queue = std::make_shared<UiQueue>(*receiver);
    ASSERT_TRUE(queue->Post([calls] { ++*calls; }));
    queue->Close();
    queue->Close();
    EXPECT_FALSE(queue->Post([calls] { ++*calls; }));
    Dispatch();
    EXPECT_EQ(*calls, 0);
}

TEST(RdpUiQueue, ShutdownFromCallbackCancelsFollowingDispatch) {
    auto receiver = std::make_unique<QObject>();
    auto queue = std::make_shared<UiQueue>(*receiver);
    auto calls = std::make_shared<int>(0);
    const auto weak = std::weak_ptr<UiQueue>{queue};
    ASSERT_TRUE(queue->Post([weak, calls] {
        if (const auto owner = weak.lock()) {
            ++*calls;
            owner->Close();
        }
    }));
    ASSERT_TRUE(queue->Post([calls] { ++*calls; }));
    Dispatch();
    EXPECT_EQ(*calls, 1);
}

TEST(RdpUiQueue, DestructionAndExpiredOwnerAreSafeWithPendingWork) {
    auto calls = std::make_shared<int>(0);
    for (int iteration{}; iteration < 30; ++iteration) {
        auto receiver = std::make_unique<QObject>();
        auto queue = std::make_shared<UiQueue>(*receiver);
        auto owner = std::make_shared<int>(0);
        const auto weak = std::weak_ptr<int>{owner};
        ASSERT_TRUE(queue->Post([weak, calls] {
            if (const auto live = weak.lock()) {
                ++*live;
                ++*calls;
            }
        }));
        owner.reset();
        Dispatch();
        ASSERT_TRUE(queue->Post([calls] { ++*calls; }));
        queue.reset();
        receiver.reset();
        Dispatch();
    }
    EXPECT_EQ(*calls, 0);
}

TEST(RdpUiQueue, BudgetRecoversAfterDispatchAndRejectsOverflow) {
    auto receiver = std::make_unique<QObject>();
    UiQueue queue{*receiver};
    auto calls = std::make_shared<int>(0);
    EXPECT_FALSE(queue.Post({}, 0));
    EXPECT_FALSE(queue.Post([calls] { ++*calls; }, std::numeric_limits<std::size_t>::max()));
    ASSERT_TRUE(queue.Post([calls] { ++*calls; }, 80 * 1024 * 1024 - 1024));
    EXPECT_FALSE(queue.Post([calls] { ++*calls; }, 0));
    Dispatch();
    for (int iteration{}; iteration < 2048; ++iteration) {
        ASSERT_TRUE(queue.Post([calls] { ++*calls; }, 0));
    }
    EXPECT_FALSE(queue.Post([calls] { ++*calls; }, 0));
    Dispatch();
    ASSERT_TRUE(queue.Post([calls] { ++*calls; }, 0));
    Dispatch();
    EXPECT_EQ(*calls, 2050);
}

TEST(RdpUiQueue, WorkerPostingRacesCloseWithoutUsingDestroyedReceiver) {
    for (int iteration{}; iteration < 30; ++iteration) {
        auto receiver = std::make_unique<QObject>();
        auto queue = std::make_shared<UiQueue>(*receiver);
        auto calls = std::make_shared<std::atomic_int>(0);
        std::jthread worker{[queue, calls] {
            for (int action{}; action < 256; ++action) {
                if (!queue->Post([calls] { calls->fetch_add(1); })) {
                    break;
                }
            }
        }};
        queue->Close();
        receiver.reset();
        worker.join();
        Dispatch();
        EXPECT_EQ(calls->load(), 0);
    }
}
} // namespace
} // namespace px::rdp

int main(int argc, char** argv) { // NOLINT(gammaray-raw-pointer-boundary): CRT/Qt startup ABI.
    ::testing::InitGoogleTest(&argc, argv);
    QCoreApplication application{argc, argv};
    return RUN_ALL_TESTS();
}
