//
// Asynchronous, serial application message dispatcher.
//

#include "message_notifier.h"

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <exception>
#include <future>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <asio2/external/asio.hpp>
#include <asio2/util/event_dispatcher.hpp>

#include "log.h"

namespace px
{
    namespace
    {
        using ErasedPayload = std::shared_ptr<const void>;
        using ErasedCallback = std::function<void(const ErasedPayload&)>;
        using Dispatcher = asio2::event_dispatcher<std::type_index, void(const ErasedPayload&)>;

        constexpr auto kSlowCallbackWarning = std::chrono::milliseconds(100);

        void UpdateMaximum(std::atomic_uint64_t& target, std::uint64_t value) {
            auto current = target.load(std::memory_order_relaxed);
            while (current < value &&
                   !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
            }
        }
    }

    class MessageListenerRegistration {
    public:
        std::type_index message_type{typeid(void)};
        Dispatcher::listener_type handle;
        std::atomic_bool active{true};
    };

    class MessageListenerState {
    public:
        MessageListenerState(const std::weak_ptr<MessageNotifierCore>& core,
                             MessageExecutor executor)
            : core_(core), executor_(std::move(executor)) {
        }

        std::weak_ptr<MessageNotifierCore> core_;
        MessageExecutor executor_;
        std::mutex registrations_mutex_;
        std::vector<std::shared_ptr<MessageListenerRegistration>> registrations_;
        std::atomic_bool active_{true};
    };

    class MessageNotifierCore : public std::enable_shared_from_this<MessageNotifierCore> {
    public:
        explicit MessageNotifierCore(std::size_t max_pending_messages)
            : max_pending_messages_(std::max<std::size_t>(1, max_pending_messages)),
              work_guard_(asio::make_work_guard(io_context_)) {
            static_assert(ASIO_VERSION == PX_ASIO_VERSION,
                          "GammaRay and asio2 must use the configured standalone Asio version");
        }

        void Run() {
            {
                std::lock_guard lock(dispatch_thread_mutex_);
                dispatch_thread_id_ = std::this_thread::get_id();
            }
            io_context_.run();
            {
                std::lock_guard lock(dispatch_thread_mutex_);
                dispatch_thread_id_ = {};
            }
            stopped_.store(true, std::memory_order_release);
            queue_idle_cv_.notify_all();
        }

        bool IsDispatchThread() const {
            std::lock_guard lock(dispatch_thread_mutex_);
            return dispatch_thread_id_ == std::this_thread::get_id();
        }

        void TrackListener(const std::shared_ptr<MessageListenerState>& state) {
            std::lock_guard lock(listener_states_mutex_);
            listener_states_.erase(
                std::remove_if(listener_states_.begin(), listener_states_.end(),
                    [](const auto& item) { return item.expired(); }),
                listener_states_.end());
            listener_states_.push_back(state);
        }

        bool Post(std::type_index message_type,
                  ErasedPayload payload,
                  bool coalesce,
                  std::uint64_t coalesce_key) {
            if (!accepting_.load(std::memory_order_acquire)) {
                rejected_.fetch_add(1, std::memory_order_relaxed);
                return false;
            }

            QueuedTask task;
            task.message_type = message_type;
            task.coalescible = coalesce;
            task.coalesce_key = coalesce_key;
            task.enqueued_at = std::chrono::steady_clock::now();
            task.callback = [self = shared_from_this(), message_type, payload = std::move(payload)]() {
                self->Dispatch(message_type, payload);
            };

            {
                std::lock_guard lock(queue_mutex_);
                if (!accepting_.load(std::memory_order_relaxed)) {
                    rejected_.fetch_add(1, std::memory_order_relaxed);
                    return false;
                }

                if (coalesce) {
                    auto existing = std::find_if(queue_.begin(), queue_.end(),
                        [message_type, coalesce_key](const QueuedTask& queued) {
                            return queued.coalescible && queued.message_type == message_type &&
                                   queued.coalesce_key == coalesce_key;
                        });
                    if (existing != queue_.end()) {
                        queue_.erase(existing);
                        coalesced_.fetch_add(1, std::memory_order_relaxed);
                    }
                }

                if (queue_.size() >= max_pending_messages_) {
                    rejected_.fetch_add(1, std::memory_order_relaxed);
                    LOGE("Message bus queue is full; reject type={}, pending={}, limit={}",
                         message_type.name(), queue_.size(), max_pending_messages_);
                    return false;
                }

                task.sequence = next_sequence_++;
                queue_.push_back(std::move(task));
                posted_.fetch_add(1, std::memory_order_relaxed);
                UpdateMaximum(high_watermark_, queue_.size());
                ScheduleDrainLocked();
            }
            return true;
        }

        std::shared_ptr<MessageListenerRegistration> AddListener(
            std::type_index message_type,
            const std::shared_ptr<MessageListenerState>& listener_state,
            ErasedCallback callback) {
            auto registration = std::make_shared<MessageListenerRegistration>();
            registration->message_type = message_type;

            auto self = shared_from_this();
            auto install = [self, message_type, listener_state, registration,
                            callback = std::move(callback)]() mutable {
                std::weak_ptr<MessageListenerState> weak_state = listener_state;
                std::weak_ptr<MessageListenerRegistration> weak_registration = registration;
                std::weak_ptr<MessageNotifierCore> weak_core = self;
                registration->handle = self->dispatcher_.append_listener(message_type,
                    [weak_core, weak_state, weak_registration, callback = std::move(callback),
                     message_type](const ErasedPayload& payload) {
                        auto state = weak_state.lock();
                        auto entry = weak_registration.lock();
                        if (!state || !entry || !state->active_.load(std::memory_order_acquire) ||
                            !entry->active.load(std::memory_order_acquire)) {
                            return;
                        }

                        auto invoke = [weak_core, weak_state, weak_registration, callback,
                                       message_type, payload]() {
                            auto invoke_state = weak_state.lock();
                            auto invoke_entry = weak_registration.lock();
                            if (!invoke_state || !invoke_entry ||
                                !invoke_state->active_.load(std::memory_order_acquire) ||
                                !invoke_entry->active.load(std::memory_order_acquire)) {
                                return;
                            }

                            const auto begin = std::chrono::steady_clock::now();
                            try {
                                callback(payload);
                            }
                            catch (const std::exception& e) {
                                if (auto core = weak_core.lock()) {
                                    core->callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                                }
                                LOGE("Message listener threw; type={}, error={}",
                                     message_type.name(), e.what());
                            }
                            catch (...) {
                                if (auto core = weak_core.lock()) {
                                    core->callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                                }
                                LOGE("Message listener threw unknown exception; type={}",
                                     message_type.name());
                            }

                            const auto elapsed = std::chrono::steady_clock::now() - begin;
                            if (elapsed >= kSlowCallbackWarning) {
                                LOGW("Slow message listener; type={}, elapsed_ms={}",
                                     message_type.name(),
                                     std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count());
                            }
                        };

                        if (state->executor_) {
                            try {
                                state->executor_(std::move(invoke));
                            }
                            catch (const std::exception& e) {
                                if (auto core = weak_core.lock()) {
                                    core->callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                                }
                                LOGE("Message listener executor threw; type={}, error={}",
                                     message_type.name(), e.what());
                            }
                            catch (...) {
                                if (auto core = weak_core.lock()) {
                                    core->callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                                }
                                LOGE("Message listener executor threw unknown exception; type={}",
                                     message_type.name());
                            }
                        }
                        else {
                            invoke();
                        }
                    });
            };

            if (!InvokeSync(std::move(install), std::chrono::seconds(5))) {
                registration->active.store(false, std::memory_order_release);
                return nullptr;
            }
            return registration;
        }

        void RemoveListeners(
            const std::vector<std::shared_ptr<MessageListenerRegistration>>& registrations) {
            if (registrations.empty()) {
                return;
            }
            for (const auto& registration : registrations) {
                if (registration) {
                    registration->active.store(false, std::memory_order_release);
                }
            }

            auto self = shared_from_this();
            auto remove = [self, registrations]() {
                for (const auto& registration : registrations) {
                    if (registration && registration->handle) {
                        self->dispatcher_.remove_listener(registration->handle);
                    }
                }
            };
            (void)InvokeSync(std::move(remove), std::chrono::seconds(5));
        }

        bool Flush(std::chrono::milliseconds timeout) {
            if (IsDispatchThread()) {
                return true;
            }
            if (stopped_.load(std::memory_order_acquire)) {
                return true;
            }

            auto& queue = queue_;
            auto& task_running = task_running_;
            auto& cancel_requested = cancel_requested_;
            auto& stopped = stopped_;
            std::unique_lock lock(queue_mutex_);
            return queue_idle_cv_.wait_for(lock, timeout, [&queue, &task_running,
                                                           &cancel_requested, &stopped]() {
                return (queue.empty() && !task_running) || cancel_requested ||
                       stopped.load(std::memory_order_acquire);
            });
        }

        void Stop(MessageBusStopMode mode) {
            bool expected = false;
            if (!stopping_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
                return;
            }
            accepting_.store(false, std::memory_order_release);

            if (mode == MessageBusStopMode::kCancel) {
                DeactivateListeners();
                {
                    std::lock_guard lock(queue_mutex_);
                    queue_.clear();
                    cancel_requested_ = true;
                }
                queue_idle_cv_.notify_all();
                work_guard_.reset();
                io_context_.stop();
                return;
            }

            if (IsDispatchThread()) {
                std::lock_guard lock(queue_mutex_);
                stop_when_idle_ = true;
                if (queue_.empty()) {
                    work_guard_.reset();
                }
                return;
            }

            // No public messages can enter after accepting_ was cleared. The
            // barrier therefore proves all earlier messages have finished.
            (void)Flush(std::chrono::seconds(10));
            DeactivateListeners();
            work_guard_.reset();
            io_context_.stop();
        }

        MessageBusStatistics Statistics() const {
            MessageBusStatistics result;
            result.posted = posted_.load(std::memory_order_relaxed);
            result.dispatched = dispatched_.load(std::memory_order_relaxed);
            result.rejected = rejected_.load(std::memory_order_relaxed);
            result.coalesced = coalesced_.load(std::memory_order_relaxed);
            result.callback_exceptions = callback_exceptions_.load(std::memory_order_relaxed);
            result.high_watermark = high_watermark_.load(std::memory_order_relaxed);
            {
                std::lock_guard lock(queue_mutex_);
                result.pending = queue_.size();
            }
            return result;
        }

    private:
        struct QueuedTask {
            std::uint64_t sequence = 0;
            std::type_index message_type{typeid(void)};
            bool coalescible = false;
            std::uint64_t coalesce_key = 0;
            std::chrono::steady_clock::time_point enqueued_at;
            std::function<void()> callback;
        };

        bool InvokeSync(std::function<void()>&& callback, std::chrono::milliseconds timeout) {
            if (IsDispatchThread()) {
                callback();
                return true;
            }
            if (stopping_.load(std::memory_order_acquire) ||
                stopped_.load(std::memory_order_acquire)) {
                return false;
            }

            auto done = std::make_shared<std::promise<void>>();
            auto future = done->get_future();
            if (!EnqueueInternal([callback = std::move(callback), done]() mutable {
                    callback();
                    done->set_value();
                })) {
                return false;
            }
            return future.wait_for(timeout) == std::future_status::ready;
        }

        bool EnqueueInternal(std::function<void()>&& callback) {
            if (stopped_.load(std::memory_order_acquire)) {
                return false;
            }
            QueuedTask task;
            task.enqueued_at = std::chrono::steady_clock::now();
            task.callback = std::move(callback);
            {
                std::lock_guard lock(queue_mutex_);
                if (cancel_requested_) {
                    return false;
                }
                task.sequence = next_sequence_++;
                queue_.push_back(std::move(task));
                UpdateMaximum(high_watermark_, queue_.size());
                ScheduleDrainLocked();
            }
            return true;
        }

        void ScheduleDrainLocked() {
            if (drain_scheduled_) {
                return;
            }
            drain_scheduled_ = true;
            asio::post(io_context_, [self = shared_from_this()]() { self->Drain(); });
        }

        void Drain() {
            for (;;) {
                QueuedTask task;
                {
                    std::lock_guard lock(queue_mutex_);
                    if (cancel_requested_) {
                        queue_.clear();
                        drain_scheduled_ = false;
                        queue_idle_cv_.notify_all();
                        return;
                    }
                    if (queue_.empty()) {
                        drain_scheduled_ = false;
                        queue_idle_cv_.notify_all();
                        if (stop_when_idle_) {
                            DeactivateListeners();
                            work_guard_.reset();
                        }
                        return;
                    }
                    task = std::move(queue_.front());
                    queue_.pop_front();
                    task_running_ = true;
                }

                try {
                    if (task.callback) {
                        task.callback();
                    }
                }
                catch (const std::exception& e) {
                    callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                    LOGE("Message bus task threw; type={}, error={}", task.message_type.name(), e.what());
                }
                catch (...) {
                    callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                    LOGE("Message bus task threw unknown exception; type={}", task.message_type.name());
                }

                {
                    std::lock_guard lock(queue_mutex_);
                    task_running_ = false;
                    if (queue_.empty()) {
                        queue_idle_cv_.notify_all();
                    }
                }
            }
        }

        void Dispatch(std::type_index message_type, const ErasedPayload& payload) {
            dispatcher_.direct_dispatch(message_type, payload);
            dispatched_.fetch_add(1, std::memory_order_relaxed);
        }

        void DeactivateListeners() {
            std::lock_guard lock(listener_states_mutex_);
            for (const auto& weak_state : listener_states_) {
                if (auto state = weak_state.lock()) {
                    state->active_.store(false, std::memory_order_release);
                }
            }
            listener_states_.clear();
        }

        const std::size_t max_pending_messages_;
        asio::io_context io_context_;
        asio::executor_work_guard<asio::io_context::executor_type> work_guard_;
        Dispatcher dispatcher_;

        mutable std::mutex dispatch_thread_mutex_;
        std::thread::id dispatch_thread_id_{};

        std::mutex listener_states_mutex_;
        std::vector<std::weak_ptr<MessageListenerState>> listener_states_;

        mutable std::mutex queue_mutex_;
        std::condition_variable queue_idle_cv_;
        std::deque<QueuedTask> queue_;
        std::uint64_t next_sequence_ = 1;
        bool drain_scheduled_ = false;
        bool stop_when_idle_ = false;
        bool cancel_requested_ = false;
        bool task_running_ = false;

        std::atomic_bool accepting_{true};
        std::atomic_bool stopping_{false};
        std::atomic_bool stopped_{false};
        std::atomic_uint64_t posted_{0};
        std::atomic_uint64_t dispatched_{0};
        std::atomic_uint64_t rejected_{0};
        std::atomic_uint64_t coalesced_{0};
        std::atomic_uint64_t callback_exceptions_{0};
        std::atomic_uint64_t high_watermark_{0};
    };

    class MessageNotifier::WorkerOwner {
    public:
        explicit WorkerOwner(const std::shared_ptr<MessageNotifierCore>& core)
            : worker_([core]() { core->Run(); }) {
        }

        ~WorkerOwner() {
            Join();
        }

        void Join() {
            std::thread worker;
            bool join_on_reaper = false;
            {
                // Move ownership before waiting. Holding join_mutex_ across
                // join() can deadlock if a listener concurrently calls Stop().
                std::lock_guard lock(join_mutex_);
                if (!worker_.joinable()) {
                    return;
                }
                join_on_reaper = worker_.get_id() == std::this_thread::get_id();
                worker = std::move(worker_);
            }

            if (join_on_reaper) {
                std::thread([worker = std::move(worker)]() mutable { worker.join(); }).detach();
            }
            else {
                worker.join();
            }
        }

    private:
        std::mutex join_mutex_;
        std::thread worker_;
    };

    MessageListener::MessageListener(const std::shared_ptr<MessageListenerState>& state)
        : state_(state) {
    }

    MessageListener::~MessageListener() {
        UnListenAll();
    }

    void MessageListener::ListenErased(std::type_index message_type, ErasedCallback&& callback) {
        if (!state_ || !state_->active_.load(std::memory_order_acquire)) {
            return;
        }
        auto core = state_->core_.lock();
        if (!core) {
            return;
        }
        auto registration = core->AddListener(message_type, state_, std::move(callback));
        if (!registration) {
            return;
        }
        std::lock_guard lock(state_->registrations_mutex_);
        if (!state_->active_.load(std::memory_order_relaxed)) {
            registration->active.store(false, std::memory_order_release);
            core->RemoveListeners({registration});
            return;
        }
        state_->registrations_.push_back(std::move(registration));
    }

    void MessageListener::UnListenAll() const {
        if (!state_) {
            return;
        }
        state_->active_.store(false, std::memory_order_release);

        std::vector<std::shared_ptr<MessageListenerRegistration>> registrations;
        {
            std::lock_guard lock(state_->registrations_mutex_);
            registrations.swap(state_->registrations_);
        }
        if (auto core = state_->core_.lock(); core) {
            core->RemoveListeners(registrations);
        }
    }

    MessageNotifier::MessageNotifier(std::size_t max_pending_messages)
        : core_(std::make_shared<MessageNotifierCore>(max_pending_messages)),
          worker_(std::make_unique<WorkerOwner>(core_)) {
    }

    MessageNotifier::~MessageNotifier() {
        Stop(MessageBusStopMode::kDrain);
    }

    std::shared_ptr<MessageListener> MessageNotifier::CreateListener(MessageExecutor executor) {
        auto state = std::make_shared<MessageListenerState>(core_, std::move(executor));
        if (core_) {
            core_->TrackListener(state);
        }
        return std::make_shared<MessageListener>(state);
    }

    bool MessageNotifier::PostErased(std::type_index message_type,
                                     ErasedPayload payload,
                                     bool coalesce,
                                     std::uint64_t coalesce_key) {
        return core_ && core_->Post(message_type, std::move(payload), coalesce, coalesce_key);
    }

    void MessageNotifier::Stop(MessageBusStopMode mode) {
        if (core_) {
            core_->Stop(mode);
        }
        if (worker_) {
            worker_->Join();
        }
    }

    bool MessageNotifier::FlushForTest(std::chrono::milliseconds timeout) {
        return core_ && core_->Flush(timeout);
    }

    bool MessageNotifier::IsDispatchThread() const {
        return core_ && core_->IsDispatchThread();
    }

    MessageBusStatistics MessageNotifier::GetStatistics() const {
        return core_ ? core_->Statistics() : MessageBusStatistics{};
    }
}
