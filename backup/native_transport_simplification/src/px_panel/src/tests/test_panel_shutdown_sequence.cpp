#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "render_panel/panel_shutdown_sequence.h"

namespace px {
namespace {

struct ScheduledTask final {
    PanelShutdownSequence::Task task;
    int delay_ms = 0;
};

class ManualSchedulerState final {
public:
    void Schedule(PanelShutdownSequence::Task task, int delay_ms) {
        std::lock_guard lock(mutex_);
        tasks_.push_back({std::move(task), delay_ms});
    }

    [[nodiscard]] size_t Count() const {
        std::lock_guard lock(mutex_);
        return tasks_.size();
    }

    ScheduledTask PopFront() {
        std::lock_guard lock(mutex_);
        if (tasks_.empty()) {
            return {};
        }
        auto task = std::move(tasks_.front());
        tasks_.erase(tasks_.begin());
        return task;
    }

private:
    mutable std::mutex mutex_;
    std::vector<ScheduledTask> tasks_;
};

PanelShutdownSequence::Scheduler MakeScheduler(
    const std::shared_ptr<ManualSchedulerState>& state) {
    return [state](PanelShutdownSequence::Task task, int delay_ms) {
        state->Schedule(std::move(task), delay_ms);
    };
}

TEST(PanelShutdownSequenceTest, SuccessfulHelperOrdering) {
    const auto ui = std::make_shared<ManualSchedulerState>();
    const auto worker = std::make_shared<ManualSchedulerState>();
    const auto stages = std::make_shared<std::vector<int>>();
    const auto sequence = PanelShutdownSequence::Make(
        MakeScheduler(ui), MakeScheduler(worker), {
            .prepare = [stages]() { stages->push_back(1); },
            .launch_service_helper = [stages]() {
                stages->push_back(2);
                return true;
            },
            .fallback_cleanup = [stages]() { stages->push_back(3); },
        });

    sequence->Start();
    sequence->Start();
    ASSERT_EQ(ui->Count(), 1U);
    auto prepare = ui->PopFront();
    EXPECT_EQ(prepare.delay_ms, 500);
    ASSERT_TRUE(prepare.task);
    prepare.task();

    ASSERT_EQ(worker->Count(), 1U);
    auto helper = worker->PopFront();
    EXPECT_EQ(helper.delay_ms, 800);
    ASSERT_TRUE(helper.task);
    helper.task();

    ASSERT_EQ(worker->Count(), 1U);
    auto fallback = worker->PopFront();
    EXPECT_EQ(fallback.delay_ms, 1500);
    ASSERT_TRUE(fallback.task);
    fallback.task();

    EXPECT_TRUE(sequence->IsCompleted());
    EXPECT_EQ(*stages, (std::vector<int>{1, 2, 3}));
}

TEST(PanelShutdownSequenceTest, FailedHelperUsesImmediateFallback) {
    const auto ui = std::make_shared<ManualSchedulerState>();
    const auto worker = std::make_shared<ManualSchedulerState>();
    const auto sequence = PanelShutdownSequence::Make(
        MakeScheduler(ui), MakeScheduler(worker), {
            .prepare = []() {},
            .launch_service_helper = []() { return false; },
            .fallback_cleanup = []() {},
        });
    sequence->Start();
    ui->PopFront().task();
    worker->PopFront().task();
    auto fallback = worker->PopFront();
    EXPECT_EQ(fallback.delay_ms, 0);
    fallback.task();
    EXPECT_TRUE(sequence->IsCompleted());
}

TEST(PanelShutdownSequenceTest, DestroyedOwnerInvalidatesQueuedStage) {
    const auto ui = std::make_shared<ManualSchedulerState>();
    const auto worker = std::make_shared<ManualSchedulerState>();
    const auto prepare_count = std::make_shared<int>(0);
    auto sequence = PanelShutdownSequence::Make(
        MakeScheduler(ui), MakeScheduler(worker), {
            .prepare = [prepare_count]() { ++(*prepare_count); },
            .launch_service_helper = []() { return true; },
            .fallback_cleanup = []() {},
        });
    sequence->Start();
    auto queued = ui->PopFront();
    sequence.reset();
    queued.task();
    EXPECT_EQ(*prepare_count, 0);
    EXPECT_EQ(worker->Count(), 0U);
}

TEST(PanelShutdownSequenceTest, HookExceptionStillReachesFallback) {
    const auto ui = std::make_shared<ManualSchedulerState>();
    const auto worker = std::make_shared<ManualSchedulerState>();
    const auto fallback_count = std::make_shared<int>(0);
    const auto sequence = PanelShutdownSequence::Make(
        MakeScheduler(ui), MakeScheduler(worker), {
            .prepare = []() { throw std::runtime_error("prepare failure"); },
            .launch_service_helper = []() {
                throw std::runtime_error("helper failure");
                return true;
            },
            .fallback_cleanup = [fallback_count]() { ++(*fallback_count); },
        });
    sequence->Start();
    ui->PopFront().task();
    worker->PopFront().task();
    auto fallback = worker->PopFront();
    EXPECT_EQ(fallback.delay_ms, 0);
    fallback.task();
    EXPECT_EQ(*fallback_count, 1);
    EXPECT_TRUE(sequence->IsCompleted());
}

}  // namespace
}  // namespace px
