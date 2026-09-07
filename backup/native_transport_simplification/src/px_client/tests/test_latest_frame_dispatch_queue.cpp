#include <gtest/gtest.h>

#include <thread>

#include "px_client/latest_frame_dispatch_queue.h"

namespace px
{
    namespace
    {
        struct TestFrame {
            int monitor_index_ = 0;
            int frame_index_ = 0;
        };
    }

    TEST(LatestFrameDispatchQueueTest, SchedulesOnlyOneUiTaskForPendingFrames) {
        LatestFrameDispatchQueue<int, TestFrame> queue;

        EXPECT_TRUE(queue.Push(0, TestFrame{0, 1}));
        EXPECT_FALSE(queue.Push(1, TestFrame{1, 2}));
        EXPECT_FALSE(queue.Push(0, TestFrame{0, 3}));
    }

    TEST(LatestFrameDispatchQueueTest, RetainsLatestFrameForEachMonitor) {
        LatestFrameDispatchQueue<int, TestFrame> queue;
        queue.Push(0, TestFrame{0, 1});
        queue.Push(1, TestFrame{1, 2});
        queue.Push(0, TestFrame{0, 3});

        const auto frames = queue.TakePending();

        ASSERT_EQ(frames.size(), 2);
        EXPECT_EQ(frames[0].monitor_index_, 0);
        EXPECT_EQ(frames[0].frame_index_, 3);
        EXPECT_EQ(frames[1].monitor_index_, 1);
        EXPECT_EQ(frames[1].frame_index_, 2);
    }

    TEST(LatestFrameDispatchQueueTest, NextFrameSchedulesAfterPendingBatchIsTaken) {
        LatestFrameDispatchQueue<int, TestFrame> queue;
        EXPECT_TRUE(queue.Push(0, TestFrame{0, 1}));
        EXPECT_EQ(queue.TakePending().size(), 1);
        EXPECT_TRUE(queue.Push(0, TestFrame{0, 2}));
    }

    TEST(LatestFrameDispatchQueueTest, StopDropsPendingFramesAndRejectsLateCallbacks) {
        LatestFrameDispatchQueue<int, TestFrame> queue;
        EXPECT_TRUE(queue.Push(0, TestFrame{0, 1}));

        queue.Stop();

        EXPECT_TRUE(queue.TakePending().empty());
        EXPECT_FALSE(queue.Push(1, TestFrame{1, 2}));
    }

    TEST(LatestFrameDispatchQueueTest, ConcurrentMonitorProducersRemainBounded) {
        LatestFrameDispatchQueue<int, TestFrame> queue;

        std::thread first_monitor([&queue]() {
            for (int frame_index = 1; frame_index <= 10000; ++frame_index) {
                queue.Push(0, TestFrame{0, frame_index});
            }
        });
        std::thread second_monitor([&queue]() {
            for (int frame_index = 1; frame_index <= 10000; ++frame_index) {
                queue.Push(1, TestFrame{1, frame_index});
            }
        });
        first_monitor.join();
        second_monitor.join();

        const auto frames = queue.TakePending();
        ASSERT_EQ(frames.size(), 2);
        EXPECT_EQ(frames[0].frame_index_, 10000);
        EXPECT_EQ(frames[1].frame_index_, 10000);
    }
}
