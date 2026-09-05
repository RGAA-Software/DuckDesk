//
// Asynchronous, serial application message dispatcher.
//

#ifndef TC_APPLICATION_MESSAGE_NOTIFIER_H
#define TC_APPLICATION_MESSAGE_NOTIFIER_H

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <typeindex>
#include <type_traits>
#include <utility>

namespace px
{
    using MessageExecutor = std::function<void(std::function<void()>)>;

    class MessageNotifierCore;
    class MessageListenerState;
    class PxAsyncRuntime;

    enum class MessageBusStopMode {
        kDrain,
        kCancel,
    };

    enum class MessageExecutionLane {
        kControl,
        kState,
        kWorker,
        kUi,
    };

    struct MessageBusLaneStatistics {
        std::uint64_t scheduled = 0;
        std::uint64_t completed = 0;
        std::uint64_t rejected = 0;
        std::uint64_t high_watermark = 0;
        std::uint64_t pending = 0;
    };

    struct MessageBusStatistics {
        std::uint64_t posted = 0;
        std::uint64_t dispatched = 0;
        std::uint64_t rejected = 0;
        std::uint64_t coalesced = 0;
        std::uint64_t callback_exceptions = 0;
        std::uint64_t high_watermark = 0;
        std::uint64_t pending = 0;
        MessageBusLaneStatistics control_lane;
        MessageBusLaneStatistics state_lane;
        MessageBusLaneStatistics worker_lane;
        MessageBusLaneStatistics ui_lane;
    };

    struct MessageNotifierOptions {
        std::size_t max_pending_messages = 4096;
        std::size_t max_state_callbacks = 1024;
        std::size_t max_worker_callbacks = 1024;
        std::size_t worker_threads = 2;
        std::shared_ptr<PxAsyncRuntime> runtime;
    };

    class MessageListener {
    public:
        explicit MessageListener(const std::shared_ptr<MessageListenerState>& state);
        ~MessageListener();

        MessageListener(const MessageListener&) = delete;
        MessageListener& operator=(const MessageListener&) = delete;

        template<typename T>
        void Listen(std::function<void(const T&)>&& cbk) {
            ListenErased(std::type_index(typeid(T)),
                [callback = std::move(cbk)](const std::shared_ptr<const void>& payload) {
                    callback(*std::static_pointer_cast<const T>(payload));
                });
        }

        void UnListenAll() const;

    private:
        friend class MessageNotifier;

        void ListenErased(std::type_index message_type,
                          std::function<void(const std::shared_ptr<const void>&)>&& callback);

        std::shared_ptr<MessageListenerState> state_;
    };

    class MessageNotifier {
    public:
        explicit MessageNotifier(std::size_t max_pending_messages = 4096);
        explicit MessageNotifier(MessageNotifierOptions options);
        ~MessageNotifier();

        MessageNotifier(const MessageNotifier&) = delete;
        MessageNotifier& operator=(const MessageNotifier&) = delete;

        // Without an executor, callbacks run serially on the message-bus
        // thread. UI consumers can provide an executor that posts the guarded
        // callback to their owning event loop.
        std::shared_ptr<MessageListener> CreateListener(MessageExecutor executor = {});

        // Control is the ordered default. State is a separate ordered lane,
        // Worker is bounded and parallel, and UI requires a guarded external
        // executor. Existing callers remain on Control unless they opt in.
        std::shared_ptr<MessageListener> CreateListener(
            MessageExecutionLane lane, MessageExecutor executor = {});

        template<typename T>
        bool PublishAppMessage(T&& message) {
            using Message = std::decay_t<T>;
            auto payload = std::make_shared<const Message>(std::forward<T>(message));
            return PostErased(std::type_index(typeid(Message)), std::move(payload), false, 0);
        }

        // Coalesced messages are intended for replaceable state updates only.
        // Messages with the same type and key that are still pending are
        // replaced by the newest value and moved to the tail of the FIFO.
        template<typename T>
        bool PublishLatestAppMessage(T&& message, std::uint64_t key = 0) {
            using Message = std::decay_t<T>;
            auto payload = std::make_shared<const Message>(std::forward<T>(message));
            return PostErased(std::type_index(typeid(Message)), std::move(payload), true, key);
        }

        // Compatibility entry point. Dispatch is asynchronous: callers must
        // never depend on listeners having run when this function returns.
        template<typename T>
        void SendAppMessage(const T& message) {
            (void)PublishAppMessage(message);
        }

        void Stop(MessageBusStopMode mode = MessageBusStopMode::kDrain);
        bool FlushForTest(std::chrono::milliseconds timeout = std::chrono::seconds(5));
        bool IsDispatchThread() const;
        [[nodiscard]] std::shared_ptr<PxAsyncRuntime> GetAsyncRuntime() const;
        MessageBusStatistics GetStatistics() const;

    private:
        bool PostErased(std::type_index message_type,
                        std::shared_ptr<const void> payload,
                        bool coalesce,
                        std::uint64_t coalesce_key);

        std::shared_ptr<MessageNotifierCore> core_;
    };
}

#endif // TC_APPLICATION_MESSAGE_NOTIFIER_H
