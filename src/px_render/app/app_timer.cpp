//
// Created by RGAA on 2024-02-05.
//

#include "app_timer.h"
#include "rd_context.h"
#include "app/app_messages.h"
#include "px_common_new/log.h"

#include <vector>
#include <format>

namespace px
{

    AppTimer::AppTimer(const std::shared_ptr<RdContext>& ctx) {
        context_ = ctx;
    }

    void AppTimer::StartTimers() {
        auto weak_self = weak_from_this();
        const auto durations = std::vector {
            kTimerDuration1000,
            kTimerDuration2000,
            kTimerDuration5000,
            kTimerDuration10S,
            kTimerDuration20S,
            kTimerDuration30S,
            kTimerDuration1Minute,
            kTimerDuration500,
            kTimerDuration100,
            kTimerDuration16,
        };
        timer_ = std::make_shared<asio2::timer>();
        for (const auto& duration : durations) {
            timer_->start_timer(std::format("tid:{}", (int)duration), (int)duration,
                                [weak_self, duration]() {
                if (auto self = weak_self.lock()) {
                    self->NotifyTimeout(duration);
                }
            });
        }
    }

    void AppTimer::StopTimers() {
        if (timer_) {
            timer_->stop_all_timers();
            timer_->stop();
            timer_->destroy();
            timer_.reset();
        }
    }

    void AppTimer::NotifyTimeout(const AppTimerDuration duration) const {
        auto context = context_.lock();
        if (!context) {
            return;
        }
        auto notifier = context->GetMessageNotifier();
        if (!notifier) {
            return;
        }
        if (duration == kTimerDuration1000) {
            (void)notifier->PublishLatestAppMessage(MsgTimer1000{});
        }
        else if (duration == kTimerDuration2000) {
            (void)notifier->PublishLatestAppMessage(MsgTimer2000{});
        }
        else if (duration == kTimerDuration5000) {
            (void)notifier->PublishLatestAppMessage(MsgTimer5000{});
        }
        else if (duration == kTimerDuration10S) {
            (void)notifier->PublishLatestAppMessage(MsgTimer10S{});
        }
        else if (duration == kTimerDuration20S) {
            (void)notifier->PublishLatestAppMessage(MsgTimer20S{});
        }
        else if (duration == kTimerDuration30S) {
            (void)notifier->PublishLatestAppMessage(MsgTimer30S{});
        }
        else if (duration == kTimerDuration500) {
            (void)notifier->PublishLatestAppMessage(MsgTimer500{});
        }
        else if (duration == kTimerDuration100) {
            (void)notifier->PublishLatestAppMessage(MsgTimer100{});
        }
        else if (duration == kTimerDuration16) {
            (void)notifier->PublishLatestAppMessage(MsgTimer16{});
        }
        else if (duration == kTimerDuration1Minute) {
            (void)notifier->PublishLatestAppMessage(MsgTimer1Minute{});
        }
    }

}
