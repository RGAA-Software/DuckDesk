//
// Created by RGAA on 19/11/2024.
//

#include "px_plugin_context.h"
#include "px_common_new/thread.h"
#include "asio2/asio2.hpp"
#include <format>

namespace px
{

    PxPluginContext::PxPluginContext(const std::string& plugin_name) {
        work_thread_ = Thread::Make(plugin_name, 1024 * 1024 * 10);
        work_thread_->Poll();

        timer_ = std::make_shared<asio2::timer>();
    }

    PxPluginContext::~PxPluginContext() {
        OnDestroy();
    }

    void PxPluginContext::OnDestroy() {
        if (destroyed_.exchange(true)) {
            return;
        }
        if (timer_) {
            timer_->stop_all_timers();
            timer_.reset();
        }
        if (work_thread_) {
            work_thread_->Exit();
            work_thread_->Clear();
            work_thread_.reset();
        }
    }

    void PxPluginContext::PostWorkTask(std::function<void()>&& task) {
        if (!destroyed_ && work_thread_ && task) {
            work_thread_->Post(std::move(task));
        }
    }

    void PxPluginContext::PostUITask(std::function<void()>&& task) {
        if (!destroyed_ && task) {
            task();
        }
    }

    void PxPluginContext::PostDelayTask(std::function<void()>&& task, int delay) {
        if (!destroyed_ && timer_ && task) {
            auto id = std::format("delay_{}", ++delay_task_id_);
            const auto weak_self = weak_from_this();
            timer_->start_timer(id, delay, 1, [weak_self, id, t = std::move(task)]() mutable {
                const auto self = weak_self.lock();
                if (!self || self->destroyed_) {
                    return;
                }
                if (self->timer_) {
                    self->timer_->stop_timer(id);
                }
                if (t) {
                    t();
                }
            });
        }
    }

    void PxPluginContext::StartTimer(int millis, std::function<void()>&& cbk) {
        if (!destroyed_ && timer_ && cbk) {
            const auto weak_self = weak_from_this();
            timer_->start_timer(std::to_string(millis), millis,
                [weak_self, callback = std::move(cbk)]() {
                    const auto self = weak_self.lock();
                    if (!self || self->destroyed_) {
                        return;
                    }
                    callback();
                });
        }
    }

}
