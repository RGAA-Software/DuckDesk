#include "voice_packet_transport.h"

#include <algorithm>
#include <utility>

namespace px {

VoicePacketTransport::~VoicePacketTransport() { Stop(); }

bool VoicePacketTransport::Start(SendCallback callback) {
    Stop();
    if (!callback) return false;
    {
        std::scoped_lock lock(mutex_);
        callback_ = std::move(callback);
        queue_.clear();
        stats_ = {};
        running_ = true;
    }
    worker_ = std::thread([this] { Worker(); });
    return true;
}

void VoicePacketTransport::Stop() {
    {
        std::scoped_lock lock(mutex_);
        running_ = false;
        queue_.clear();
        stats_.queued = 0;
    }
    cv_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::scoped_lock lock(mutex_);
    callback_ = {};
}

bool VoicePacketTransport::Enqueue(VoiceTransportPacket packet) {
    if (packet.opus.empty()) return false;
    {
        std::scoped_lock lock(mutex_);
        if (!running_) return false;
        ++stats_.enqueued;
        if (queue_.size() >= kMaxQueuedPackets) {
            queue_.pop_front();
            ++stats_.congestion_drops;
        }
        queue_.push_back(std::move(packet));
        stats_.queued = queue_.size();
        stats_.peak_queued = std::max(stats_.peak_queued, queue_.size());
    }
    cv_.notify_one();
    return true;
}

VoicePacketTransportStats VoicePacketTransport::Stats() const {
    std::scoped_lock lock(mutex_);
    auto result = stats_;
    result.queued = queue_.size();
    return result;
}

void VoicePacketTransport::Worker() {
    for (;;) {
        VoiceTransportPacket packet;
        SendCallback callback;
        {
            std::unique_lock lock(mutex_);
            cv_.wait(lock, [this] { return !running_ || !queue_.empty(); });
            if (!running_ && queue_.empty()) return;
            // When congested, jump directly to the newest packet. Speech is
            // time-sensitive and old frames are less useful than continuity.
            if (queue_.size() > 1) {
                stats_.congestion_drops += queue_.size() - 1;
                packet = std::move(queue_.back());
                queue_.clear();
            } else {
                packet = std::move(queue_.front());
                queue_.pop_front();
            }
            stats_.queued = queue_.size();
            callback = callback_;
        }
        if (callback) {
            callback(packet);
            std::scoped_lock lock(mutex_);
            ++stats_.sent;
        }
    }
}

}  // namespace px
