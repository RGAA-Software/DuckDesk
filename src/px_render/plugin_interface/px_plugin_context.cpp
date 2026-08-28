//
// Created by RGAA on 19/11/2024.
//

#include "px_plugin_context.h"
#include "px_common_new/thread.h"
#include "asio2/asio2.hpp"
#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>

namespace px
{
    namespace
    {
        // asio2::timer::stop() must run outside its own IO thread so the
        // internal worker can be joined. This reaper is only used when a
        // plug-in requests destruction from inside a timer callback.
        class PluginTimerReaper final {
        public:
            static std::shared_ptr<PluginTimerReaper> Instance() {
                static const auto instance = std::make_shared<PluginTimerReaper>();
                return instance;
            }

            PluginTimerReaper()
                : state_(std::make_shared<State>()),
                  worker_([state = state_]() {
                      for (;;) {
                          std::shared_ptr<asio2::timer> timer;
                          {
                              std::unique_lock lock(state->mutex);
                              state->condition.wait(lock, [state]() {
                                  return state->stopping || !state->timers.empty();
                              });
                              if (state->timers.empty() && state->stopping) {
                                  return;
                              }
                              timer = std::move(state->timers.front());
                              state->timers.pop_front();
                          }
                          timer->stop();
                      }
                  }) {}

            ~PluginTimerReaper() {
                {
                    std::lock_guard lock(state_->mutex);
                    state_->stopping = true;
                }
                state_->condition.notify_all();
                if (worker_.joinable()) {
                    worker_.join();
                }
            }

            PluginTimerReaper(const PluginTimerReaper&) = delete;
            PluginTimerReaper& operator=(const PluginTimerReaper&) = delete;

            void Submit(std::shared_ptr<asio2::timer> timer) {
                if (!timer) {
                    return;
                }
                {
                    std::lock_guard lock(state_->mutex);
                    state_->timers.push_back(std::move(timer));
                }
                state_->condition.notify_one();
            }

        private:
            struct State final {
                std::mutex mutex;
                std::condition_variable condition;
                std::deque<std::shared_ptr<asio2::timer>> timers;
                bool stopping = false;
            };

            std::shared_ptr<State> state_;
            std::thread worker_;
        };
    }

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
        auto timer = std::move(timer_);
        if (timer) {
            // stop_all_timers() only posts cancellation and returns. Releasing
            // the final shared_ptr immediately can therefore run asio2's timer
            // destructor on its own IO thread, where destroying a joinable
            // std::thread terminates the process. Normal host unload uses the
            // synchronous stop barrier. Shutdown requested by a timer callback
            // hands the last owned reference to a non-IO reaper to avoid a
            // self-join while retaining the same drain-before-unload contract.
            if (timer->io().running_in_this_thread()) {
                PluginTimerReaper::Instance()->Submit(std::move(timer));
            }
            else {
                timer->stop();
            }
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
