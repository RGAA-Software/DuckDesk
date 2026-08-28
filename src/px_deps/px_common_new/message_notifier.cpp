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
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <asio2/external/asio.hpp>
#include <asio2/util/event_dispatcher.hpp>

#include "async_runtime.h"
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

        struct MessageLaneCounters {
            std::atomic_uint64_t scheduled{0};
            std::atomic_uint64_t completed{0};
            std::atomic_uint64_t rejected{0};
            std::atomic_uint64_t high_watermark{0};
            std::atomic_uint64_t pending{0};

            MessageBusLaneStatistics Snapshot() const {
                MessageBusLaneStatistics result;
                result.scheduled = scheduled.load(std::memory_order_relaxed);
                result.completed = completed.load(std::memory_order_relaxed);
                result.rejected = rejected.load(std::memory_order_relaxed);
                result.high_watermark = high_watermark.load(std::memory_order_relaxed);
                result.pending = pending.load(std::memory_order_relaxed);
                return result;
            }
        };

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
                             MessageExecutionLane lane,
                             MessageExecutor executor)
            : core_(core), lane_(lane), executor_(std::move(executor)) {
        }

        std::weak_ptr<MessageNotifierCore> core_;
        MessageExecutionLane lane_ = MessageExecutionLane::kControl;
        MessageExecutor executor_;
        std::mutex registrations_mutex_;
        std::vector<std::shared_ptr<MessageListenerRegistration>> registrations_;
        std::atomic_bool active_{true};
    };

    class MessageNotifierCore : public std::enable_shared_from_this<MessageNotifierCore> {
    public:
        explicit MessageNotifierCore(MessageNotifierOptions options)
            : max_pending_messages_(std::max<std::size_t>(1, options.max_pending_messages)),
              max_state_callbacks_(std::max<std::size_t>(1, options.max_state_callbacks)),
              max_worker_callbacks_(std::max<std::size_t>(1, options.max_worker_callbacks)),
              runtime_(PxAsyncRuntime::Create({.worker_threads = options.worker_threads})) {
            static_assert(ASIO_VERSION == PX_ASIO_VERSION,
                          "GammaRay and asio2 must use the configured standalone Asio version");
            runtime_->Start();
        }

        bool IsDispatchThread() const {
            return runtime_->IsRuntimeThread();
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

                        if (auto core = weak_core.lock()) {
                            core->ScheduleInvocation(state->lane_, state->executor_,
                                                     std::move(invoke), message_type);
                        }
                    });
            };

            if (!InvokeSync(std::move(install), std::chrono::seconds(5))) {
                registration->active.store(false, std::memory_order_release);
                return nullptr;
            }
            return registration;
        }

        void ScheduleInvocation(MessageExecutionLane lane,
                                const MessageExecutor& executor,
                                std::function<void()> invocation,
                                std::type_index message_type) {
            if (executor || lane == MessageExecutionLane::kUi) {
                auto& counters = ui_lane_counters_;
                if (!executor) {
                    counters.rejected.fetch_add(1, std::memory_order_relaxed);
                    LOGE("UI message listener has no executor; type={}", message_type.name());
                    return;
                }

                counters.scheduled.fetch_add(1, std::memory_order_relaxed);
                std::weak_ptr<MessageNotifierCore> weak_self = shared_from_this();
                try {
                    executor([weak_self, invocation = std::move(invocation)]() mutable {
                        if (auto self = weak_self.lock()) {
                            self->ExecuteLaneCallback(
                                MessageExecutionLane::kUi, std::move(invocation));
                        }
                    });
                }
                catch (const std::exception& e) {
                    counters.rejected.fetch_add(1, std::memory_order_relaxed);
                    callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                    LOGE("Message listener executor threw; type={}, error={}",
                         message_type.name(), e.what());
                }
                catch (...) {
                    counters.rejected.fetch_add(1, std::memory_order_relaxed);
                    callback_exceptions_.fetch_add(1, std::memory_order_relaxed);
                    LOGE("Message listener executor threw unknown exception; type={}",
                         message_type.name());
                }
                return;
            }

            if (lane == MessageExecutionLane::kControl) {
                control_lane_counters_.scheduled.fetch_add(1, std::memory_order_relaxed);
                ExecuteLaneCallback(lane, std::move(invocation));
                return;
            }

            auto& counters = LaneCounters(lane);
            const auto limit = lane == MessageExecutionLane::kState
                ? max_state_callbacks_ : max_worker_callbacks_;
            auto pending = counters.pending.load(std::memory_order_relaxed);
            while (pending < limit &&
                   !counters.pending.compare_exchange_weak(
                       pending, pending + 1, std::memory_order_acq_rel)) {
            }
            if (pending >= limit) {
                counters.rejected.fetch_add(1, std::memory_order_relaxed);
                LOGE("Message callback lane is full; type={}, lane={}, pending={}, limit={}",
                     message_type.name(), static_cast<int>(lane), pending, limit);
                return;
            }

            counters.scheduled.fetch_add(1, std::memory_order_relaxed);
            UpdateMaximum(counters.high_watermark, pending + 1);
            std::weak_ptr<MessageNotifierCore> weak_self = shared_from_this();
            auto task = [weak_self, lane, invocation = std::move(invocation)]() mutable {
                if (auto self = weak_self.lock()) {
                    self->ExecuteQueuedLaneCallback(lane, std::move(invocation));
                }
            };
            if (lane == MessageExecutionLane::kState) {
                asio::post(runtime_->Executor(PxAsyncLane::kState), std::move(task));
            }
            else {
                asio::post(runtime_->Executor(PxAsyncLane::kWorker), std::move(task));
            }
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
            auto& state_pending = state_lane_counters_.pending;
            auto& worker_pending = worker_lane_counters_.pending;
            std::unique_lock lock(queue_mutex_);
            return queue_idle_cv_.wait_for(lock, timeout, [&queue, &task_running,
                                                           &cancel_requested, &stopped,
                                                           &state_pending, &worker_pending]() {
                return (queue.empty() && !task_running &&
                        state_pending.load(std::memory_order_acquire) == 0 &&
                        worker_pending.load(std::memory_order_acquire) == 0) ||
                       cancel_requested ||
                       stopped.load(std::memory_order_acquire);
            });
        }

        void Stop(MessageBusStopMode mode) {
            bool expected = false;
            const bool first_stop = stopping_.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel);
            if (first_stop) {
                accepting_.store(false, std::memory_order_release);

                if (mode == MessageBusStopMode::kCancel) {
                    DeactivateListeners();
                    {
                        std::lock_guard lock(queue_mutex_);
                        queue_.clear();
                        cancel_requested_ = true;
                    }
                    queue_idle_cv_.notify_all();
                    runtime_->RequestStop();
                }
                else if (IsDispatchThread()) {
                    {
                        std::lock_guard lock(queue_mutex_);
                        stop_when_idle_ = true;
                    }
                    MaybeFinishDrainStop();
                }
                else {
                    // No public messages can enter after accepting_ was
                    // cleared. The barrier proves all earlier messages and
                    // queued state/worker callbacks have finished.
                    (void)Flush(std::chrono::seconds(10));
                    DeactivateListeners();
                    runtime_->RequestDrain();
                }
            }

            runtime_->Join();
            if (!runtime_->IsRuntimeThread()) {
                stopped_.store(true, std::memory_order_release);
                queue_idle_cv_.notify_all();
            }
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
            result.control_lane = control_lane_counters_.Snapshot();
            result.state_lane = state_lane_counters_.Snapshot();
            result.worker_lane = worker_lane_counters_.Snapshot();
            result.ui_lane = ui_lane_counters_.Snapshot();
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
            if (runtime_->IsControlThread()) {
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
            asio::post(runtime_->Executor(PxAsyncLane::kControl),
                       [self = shared_from_this()]() { self->Drain(); });
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
                        break;
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
            MaybeFinishDrainStop();
        }

        void Dispatch(std::type_index message_type, const ErasedPayload& payload) {
            dispatcher_.direct_dispatch(message_type, payload);
            dispatched_.fetch_add(1, std::memory_order_relaxed);
        }

        MessageLaneCounters& LaneCounters(MessageExecutionLane lane) {
            switch (lane) {
            case MessageExecutionLane::kControl:
                return control_lane_counters_;
            case MessageExecutionLane::kState:
                return state_lane_counters_;
            case MessageExecutionLane::kWorker:
                return worker_lane_counters_;
            case MessageExecutionLane::kUi:
                return ui_lane_counters_;
            }
            return control_lane_counters_;
        }

        void ExecuteLaneCallback(MessageExecutionLane lane,
                                 std::function<void()> invocation) {
            if (invocation) {
                invocation();
            }
            LaneCounters(lane).completed.fetch_add(1, std::memory_order_relaxed);
        }

        void ExecuteQueuedLaneCallback(MessageExecutionLane lane,
                                       std::function<void()> invocation) {
            ExecuteLaneCallback(lane, std::move(invocation));
            LaneCounters(lane).pending.fetch_sub(1, std::memory_order_acq_rel);
            queue_idle_cv_.notify_all();
            MaybeFinishDrainStop();
        }

        void MaybeFinishDrainStop() {
            bool finish = false;
            {
                std::lock_guard lock(queue_mutex_);
                finish = stop_when_idle_ && queue_.empty() && !task_running_ &&
                    state_lane_counters_.pending.load(std::memory_order_acquire) == 0 &&
                    worker_lane_counters_.pending.load(std::memory_order_acquire) == 0;
                if (finish) {
                    stop_when_idle_ = false;
                }
            }
            if (finish) {
                DeactivateListeners();
                runtime_->RequestDrain();
                stopped_.store(true, std::memory_order_release);
                queue_idle_cv_.notify_all();
            }
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
        const std::size_t max_state_callbacks_;
        const std::size_t max_worker_callbacks_;
        std::shared_ptr<PxAsyncRuntime> runtime_;
        Dispatcher dispatcher_;

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
        MessageLaneCounters control_lane_counters_;
        MessageLaneCounters state_lane_counters_;
        MessageLaneCounters worker_lane_counters_;
        MessageLaneCounters ui_lane_counters_;
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
        : MessageNotifier(MessageNotifierOptions{
              .max_pending_messages = max_pending_messages}) {
    }

    MessageNotifier::MessageNotifier(MessageNotifierOptions options)
        : core_(std::make_shared<MessageNotifierCore>(std::move(options))) {
    }

    MessageNotifier::~MessageNotifier() {
        Stop(MessageBusStopMode::kDrain);
    }

    std::shared_ptr<MessageListener> MessageNotifier::CreateListener(MessageExecutor executor) {
        const auto lane = executor
            ? MessageExecutionLane::kUi : MessageExecutionLane::kControl;
        return CreateListener(lane, std::move(executor));
    }

    std::shared_ptr<MessageListener> MessageNotifier::CreateListener(
        MessageExecutionLane lane, MessageExecutor executor) {
        auto state = std::make_shared<MessageListenerState>(
            core_, lane, std::move(executor));
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
