#include "client_module_context.h"

#include <condition_variable>
#include <deque>
#include <format>
#include <mutex>
#include <thread>

#include "asio2/asio2.hpp"
#include "px_common_new/thread.h"

namespace px {
namespace {

class ClientModuleTimerReaper final {
public:
    static std::shared_ptr<ClientModuleTimerReaper> Instance() {
        static const auto instance = std::make_shared<ClientModuleTimerReaper>();
        return instance;
    }

    ClientModuleTimerReaper()
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
          }) {
    }

    ~ClientModuleTimerReaper() {
        {
            std::lock_guard lock(state_->mutex);
            state_->stopping = true;
        }
        state_->condition.notify_all();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    ClientModuleTimerReaper(const ClientModuleTimerReaper&) = delete;
    ClientModuleTimerReaper& operator=(const ClientModuleTimerReaper&) = delete;

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

ClientModuleContext::ClientModuleContext(const std::string& module_name)
    : work_thread_(Thread::Make(module_name, 1024 * 1024 * 10)),
      timer_(std::make_shared<asio2::timer>()) {
    work_thread_->Poll();
}

ClientModuleContext::~ClientModuleContext() {
    Stop();
}

void ClientModuleContext::Stop() {
    if (stopped_.exchange(true)) {
        return;
    }
    std::shared_ptr<asio2::timer> timer;
    std::shared_ptr<Thread> work_thread;
    {
        std::lock_guard lock(lifecycle_mutex_);
        timer = std::move(timer_);
        work_thread = std::move(work_thread_);
    }
    if (timer) {
        if (timer->io().running_in_this_thread()) {
            ClientModuleTimerReaper::Instance()->Submit(std::move(timer));
        } else {
            timer->stop();
        }
    }
    if (work_thread) {
        work_thread->Exit();
        work_thread->Clear();
    }
}

void ClientModuleContext::PostWorkTask(std::function<void()>&& task) {
    if (!task) {
        return;
    }
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_ && work_thread_) {
        work_thread_->Post(std::move(task));
    }
}

void ClientModuleContext::PostUITask(std::function<void()>&& task) {
    if (stopped_ || !task) {
        return;
    }
    const auto weak_self = weak_from_this();
    QMetaObject::invokeMethod(this, [weak_self, task = std::move(task)]() {
        const auto self = weak_self.lock();
        if (!self || self->stopped_) {
            return;
        }
        task();
    });
}

void ClientModuleContext::PostDelayTask(
    std::function<void()>&& task,
    int delay_ms) {
    if (!task) {
        return;
    }
    std::lock_guard lock(lifecycle_mutex_);
    if (stopped_ || !timer_) {
        return;
    }
    const auto id = std::format("client_module_delay_{}", ++delay_task_id_);
    const auto weak_self = weak_from_this();
    timer_->start_timer(
        id, delay_ms, 1,
        [weak_self, task = std::move(task)]() mutable {
            const auto self = weak_self.lock();
            if (!self || self->stopped_) {
                return;
            }
            task();
        });
}

void ClientModuleContext::StartTimer(
    int millis,
    std::function<void()>&& callback) {
    if (!callback) {
        return;
    }
    std::lock_guard lock(lifecycle_mutex_);
    if (stopped_ || !timer_) {
        return;
    }
    const auto weak_self = weak_from_this();
    timer_->start_timer(
        std::to_string(millis), millis,
        [weak_self, callback = std::move(callback)]() {
            const auto self = weak_self.lock();
            if (!self || self->stopped_) {
                return;
            }
            callback();
        });
}

}  // namespace px
