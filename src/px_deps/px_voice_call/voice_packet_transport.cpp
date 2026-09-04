#include "voice_packet_transport.h"
#include "px_common_new/async_runtime.h"

#include <algorithm>
#include <utility>

namespace px {

struct VoicePacketTransport::WorkerState final {
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::deque<VoiceTransportPacket> queue;
    SendCallback callback;
    bool running = false;
    VoicePacketTransportStats stats;
};

VoicePacketTransport::~VoicePacketTransport() { Stop(); }

bool VoicePacketTransport::Start(SendCallback callback) {
    if (!callback) {
        return false;
    }
    std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    StopLocked();
    last_stats_ = {};
    auto state = std::make_shared<WorkerState>();
    {
        std::scoped_lock lock(state->mutex);
        state->callback = std::move(callback);
        state->running = true;
    }
    state_ = state;
    worker_ = std::thread([state] { WorkerMain(state); });
    return true;
}

void VoicePacketTransport::Stop() {
    std::scoped_lock lifecycle_lock(lifecycle_mutex_);
    StopLocked();
}

void VoicePacketTransport::StopLocked() {
    const auto state = std::move(state_);
    if (!state) {
        if (worker_.joinable()) {
            worker_.join();
        }
        return;
    }
    {
        std::scoped_lock lock(state->mutex);
        state->running = false;
        state->queue.clear();
        state->stats.queued = 0;
    }
    state->condition.notify_all();
    if (worker_.joinable()) {
        if (worker_.get_id() == std::this_thread::get_id()) {
            // The worker retains only WorkerState, so detaching here is safe
            // and lets a delivery callback stop or destroy its owner without
            // attempting to join itself.
            PxAsyncRuntime::DeferJoin(std::move(worker_));
        }
        else {
            worker_.join();
        }
    }
    std::scoped_lock lock(state->mutex);
    state->callback = {};
    last_stats_ = state->stats;
    last_stats_.queued = state->queue.size();
}

bool VoicePacketTransport::Enqueue(VoiceTransportPacket packet) {
    if (packet.opus.empty()) {
        return false;
    }
    std::shared_ptr<WorkerState> state;
    {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        state = state_;
    }
    if (!state) {
        return false;
    }
    {
        std::scoped_lock lock(state->mutex);
        if (!state->running) {
            return false;
        }
        ++state->stats.enqueued;
        if (state->queue.size() >= kMaxQueuedPackets) {
            state->queue.pop_front();
            ++state->stats.congestion_drops;
        }
        state->queue.push_back(std::move(packet));
        state->stats.queued = state->queue.size();
        state->stats.peak_queued = std::max(
            state->stats.peak_queued, state->queue.size());
    }
    state->condition.notify_one();
    return true;
}

VoicePacketTransportStats VoicePacketTransport::Stats() const {
    std::shared_ptr<WorkerState> state;
    {
        std::scoped_lock lifecycle_lock(lifecycle_mutex_);
        state = state_;
    }
    if (!state) {
        return last_stats_;
    }
    std::scoped_lock lock(state->mutex);
    auto result = state->stats;
    result.queued = state->queue.size();
    return result;
}

void VoicePacketTransport::WorkerMain(
    const std::shared_ptr<WorkerState>& state) {
    for (;;) {
        VoiceTransportPacket packet;
        SendCallback callback;
        {
            std::unique_lock lock(state->mutex);
            state->condition.wait(lock, [state] {
                return !state->running || !state->queue.empty();
            });
            if (!state->running && state->queue.empty()) {
                return;
            }
            // When congested, jump directly to the newest packet. Speech is
            // time-sensitive and old frames are less useful than continuity.
            if (state->queue.size() > 1) {
                state->stats.congestion_drops += state->queue.size() - 1;
                packet = std::move(state->queue.back());
                state->queue.clear();
            } else {
                packet = std::move(state->queue.front());
                state->queue.pop_front();
            }
            state->stats.queued = state->queue.size();
            callback = state->callback;
        }
        if (callback) {
            callback(packet);
            std::scoped_lock lock(state->mutex);
            ++state->stats.sent;
        }
    }
}

}  // namespace px
