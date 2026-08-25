//
// Created by RGAA on 2024-02-05.
//

#include "sdk_timer.h"
#include "px_common_new/message_notifier.h"
#include "sdk_messages.h"
#include "px_common_new/log.h"

#include <vector>
#include <format>

namespace px
{

    SdkTimer::SdkTimer(const std::shared_ptr<MessageNotifier>& notifier) {
        notifier_ = notifier;
        timer_ = std::make_shared<asio2::timer>();
    }

    void SdkTimer::StartTimers() {
        auto weak_self = weak_from_this();
        auto durations = std::vector<SdkTimerDuration>{
            kTimerDuration1000, kTimerDuration2000, kTimerDuration100, kTimerDuration16
        };
        for (const auto& duration : durations) {
            auto timer_id = std::format("tid:{}", (int)duration);
            timer_->start_timer(timer_id, (int)duration, [weak_self, duration]() {
                if (auto self = weak_self.lock()) {
                    self->NotifyTimeout(duration);
                }
            });
        }
    }

    void SdkTimer::Exit() {
        if (timer_) {
            timer_->stop_all_timers();
            timer_->stop();
            timer_->destroy();
        }
    }

    void SdkTimer::NotifyTimeout(SdkTimerDuration duration) {
        if (duration == SdkTimerDuration::kTimerDuration1000) {
            (void)notifier_->PublishLatestAppMessage(SdkMsgTimer1000{});
        }
        else if (duration == SdkTimerDuration::kTimerDuration2000) {
            (void)notifier_->PublishLatestAppMessage(SdkMsgTimer2000{});
        }
        else if (duration == SdkTimerDuration::kTimerDuration100) {
            (void)notifier_->PublishLatestAppMessage(SdkMsgTimer100{});
        }
        else if (duration == SdkTimerDuration::kTimerDuration16) {
            // Timer ticks represent current time, not accumulated work. Keep at
            // most one pending tick when a listener is temporarily busy.
            (void)notifier_->PublishLatestAppMessage(SdkMsgTimer16{});
        }
    }

}
