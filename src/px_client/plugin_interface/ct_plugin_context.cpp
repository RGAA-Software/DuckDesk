//
// Created by RGAA on 19/11/2024.
//

#include "ct_plugin_context.h"
#include "px_common_new/thread.h"
#include "asio2/asio2.hpp"

#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>

namespace px
{

    namespace {

        class ClientPluginTimerReaper final {
        public:
            static std::shared_ptr<ClientPluginTimerReaper> Instance() {
                static const auto instance =
                    std::make_shared<ClientPluginTimerReaper>();
                return instance;
            }

            ClientPluginTimerReaper()
                : state_(std::make_shared<State>()),
                  worker_([state = state_]() {
                      for (;;) {
                          std::shared_ptr<asio2::timer> timer;
                          {
                              std::unique_lock lock(state->mutex);
                              state->condition.wait(lock, [state]() {
                                  return state->stopping ||
                                      !state->timers.empty();
                              });
                              if (state->timers.empty() && state->stopping) {
                                  return;
                              }
                              timer = std::move(state->timers.front());
                              state->timers.pop_front();
                          }
                          timer->stop();
                      }
                  }) {
            }

            ~ClientPluginTimerReaper() {
                {
                    std::lock_guard lock(state_->mutex);
                    state_->stopping = true;
                }
                state_->condition.notify_all();
                if (worker_.joinable()) {
                    worker_.join();
                }
            }

            ClientPluginTimerReaper(const ClientPluginTimerReaper&) = delete;
            ClientPluginTimerReaper& operator=(
                const ClientPluginTimerReaper&) = delete;

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

    }  // namespace

    ClientPluginContext::ClientPluginContext(const std::string& plugin_name) {
        work_thread_ = Thread::Make(plugin_name, 1024 * 1024 * 10);
        work_thread_->Poll();

        timer_ = std::make_shared<asio2::timer>();
    }

    ClientPluginContext::~ClientPluginContext() {
        OnDestroy();
    }

    void ClientPluginContext::OnDestroy() {
        if (destroyed_.exchange(true)) {
            return;
        }
        auto timer = std::move(timer_);
        if (timer) {
            if (timer->io().running_in_this_thread()) {
                ClientPluginTimerReaper::Instance()->Submit(std::move(timer));
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

    void ClientPluginContext::PostWorkTask(std::function<void()>&& task) {
        if (!destroyed_ && work_thread_ && task) {
            work_thread_->Post(std::move(task));
        }
    }

    void ClientPluginContext::PostUITask(std::function<void()>&& task) {
        if (destroyed_ || !task) {
            return;
        }
        const auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, t = std::move(task)]() {
            const auto self = weak_self.lock();
            if (!self || self->destroyed_) {
                return;
            }
            t();
        });
    }

    void ClientPluginContext::PostDelayTask(
        std::function<void()>&& task, int delay_ms) {
        if (destroyed_ || !timer_ || !task) {
            return;
        }
        const auto id = std::format("client_delay_{}", ++delay_task_id_);
        const auto weak_self = weak_from_this();
        timer_->start_timer(
            id, delay_ms, 1,
            [weak_self, id, t = std::move(task)]() mutable {
                const auto self = weak_self.lock();
                if (!self || self->destroyed_) {
                    return;
                }
                if (self->timer_) {
                    self->timer_->stop_timer(id);
                }
                t();
            });
    }

    void ClientPluginContext::StartTimer(int millis, std::function<void()>&& cbk) {
        if (!destroyed_ && timer_ && cbk) {
            const auto weak_self = weak_from_this();
            timer_->start_timer(
                std::to_string(millis), millis,
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
