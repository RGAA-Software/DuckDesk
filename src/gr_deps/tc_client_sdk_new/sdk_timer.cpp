//
// Created by RGAA on 2024-02-05.
//

#include "sdk_timer.h"
#include "tc_common_new/message_notifier.h"
#include "sdk_messages.h"
#include "tc_common_new/log.h"

#include <vector>
#include <format>

namespace tc
{

    SdkTimer::SdkTimer(const std::shared_ptr<MessageNotifier>& notifier) {
        notifier_ = notifier;
        timer_ = std::make_shared<asio2::timer>();
    }

    void SdkTimer::StartTimers() {
        auto durations = std::vector<SdkTimerDuration>{
            kTimerDuration1000, kTimerDuration2000, kTimerDuration100, kTimerDuration16
        };
        for (const auto& duration : durations) {
            auto timer_id = std::format("tid:{}", (int)duration);
            timer_->start_timer(timer_id, (int)duration, [=, this]() {
                this->NotifyTimeout(duration);
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
            notifier_->SendAppMessage(SdkMsgTimer1000{});
        }
        else if (duration == SdkTimerDuration::kTimerDuration2000) {
            notifier_->SendAppMessage(SdkMsgTimer2000{});
        }
        else if (duration == SdkTimerDuration::kTimerDuration100) {
            notifier_->SendAppMessage(SdkMsgTimer100{});
        }
        else if (duration == SdkTimerDuration::kTimerDuration16) {
            notifier_->SendAppMessage(SdkMsgTimer16{});
        }
    }

}