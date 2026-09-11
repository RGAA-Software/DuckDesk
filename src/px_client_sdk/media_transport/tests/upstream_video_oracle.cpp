#include "upstream_video_oracle.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <stdexcept>

extern "C" {
#include "Limelight-internal.h"
#include <rs.h>
// These globals are required by the unchanged upstream C implementation, not product state.
int AppVersionQuad[4]{7, 1, 431, 0};
STREAM_CONFIGURATION StreamConfig{};
CONNECTION_LISTENER_CALLBACKS ListenerCallbacks{};
int AudioPacketDuration{20};
}

namespace {
thread_local std::shared_ptr<px::media::test::OracleResults> active_results{};
thread_local std::uint64_t oracle_now_us{1000000};
std::mutex oracle_mutex{}; // Serializes the unchanged upstream process globals in this test executable only.
struct BufferCloser final {
    void operator()(void* buffer) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): upstream malloc/free ownership boundary.
        std::free(buffer);
    }
};
using BufferOwner = std::unique_ptr<void, BufferCloser>;
} // namespace

extern "C" {
void connectionSendFrameFecStatus(PSS_FRAME_FEC_STATUS status) { // NOLINT(gammaray-raw-pointer-boundary): borrowed upstream callback ABI.
    static_cast<void>(status);
    if (active_results)
        ++active_results->fec_reports;
}
void notifyFrameLost(unsigned int frame, bool speculative) {
    if (active_results)
        active_results->losses.push_back({frame, speculative});
}
void connectionSawFrame(unsigned int frame) {
    static_cast<void>(frame);
}
uint64_t PltGetMicroseconds(void) {
    return oracle_now_us;
}
void queueRtpPacket(PRTPV_QUEUE_ENTRY entry) { // NOLINT(gammaray-raw-pointer-boundary): upstream transfers malloc packet ownership to callback.
    const BufferOwner owner{entry->packet};
    if (active_results) {
        const auto bytes = std::span<const std::uint8_t>{static_cast<const std::uint8_t*>(owner.get()), static_cast<std::size_t>(entry->length)};
        active_results->data_packets.emplace_back(bytes.begin(), bytes.end());
    }
}
}

namespace px::media::test {
struct UpstreamAudioOracle::State final {
    std::unique_lock<std::mutex> lock{oracle_mutex, std::try_to_lock};
    RTP_AUDIO_QUEUE queue{};
    bool initialized{};
    ~State() {
        if (initialized)
            RtpaCleanupQueue(&queue);
    }
};
UpstreamAudioOracle::UpstreamAudioOracle() : state_(std::make_unique<State>()) {
    if (!state_->lock.owns_lock())
        throw std::logic_error("Only one upstream reference queue may be active");
    RtpaInitializeQueue(&state_->queue);
    state_->initialized = true;
}
UpstreamAudioOracle::~UpstreamAudioOracle() = default;
std::vector<AudioDelivery> UpstreamAudioOracle::Feed(const Packet& packet, std::uint64_t now_us) {
    std::vector<AudioDelivery> output{};
    if (packet.size() < sizeof(RTP_PACKET) || packet.size() > 1500)
        throw std::invalid_argument("Invalid oracle audio packet");
    oracle_now_us = now_us;
    BufferOwner storage{std::malloc(packet.size())};
    if (!storage)
        throw std::bad_alloc();
    std::memcpy(storage.get(), packet.data(), packet.size());
    auto& rtp = *static_cast<PRTP_PACKET>(storage.get());
    rtp.sequenceNumber = BE16(rtp.sequenceNumber);
    rtp.timestamp = BE32(rtp.timestamp);
    rtp.ssrc = BE32(rtp.ssrc);
    const auto result = RtpaAddPacket(&state_->queue, &rtp, static_cast<std::uint16_t>(packet.size()));
    if (RTPQ_HANDLE_NOW(result)) {
        output.push_back({rtp.sequenceNumber, Packet(packet.begin() + sizeof(RTP_PACKET), packet.end())});
    } else if (RTPQ_PACKET_READY(result)) {
        for (;;) {
            const auto sequence = state_->queue.nextRtpSequenceNumber;
            std::uint16_t length{};
            const BufferOwner queued{RtpaGetQueuedPacket(&state_->queue, 1, &length)};
            if (!queued)
                break;
            const auto bytes = std::span<const std::uint8_t>{static_cast<const std::uint8_t*>(queued.get()), static_cast<std::size_t>(length) + 1};
            output.push_back({sequence, length == 0 ? Packet{} : Packet(bytes.begin() + 1 + sizeof(RTP_PACKET), bytes.end())});
        }
    }
    return output;
}
bool DecodeWithUpstreamAudioMatrix(std::vector<Packet>& shards, std::span<const std::uint8_t> missing) {
    if (shards.size() != 6 || missing.size() != 6 || shards.front().empty())
        return false;
    const std::lock_guard lock(oracle_mutex);
    struct AudioState final {
        RTP_AUDIO_QUEUE queue{};
        AudioState() {
            RtpaInitializeQueue(&queue);
        }
        ~AudioState() {
            RtpaCleanupQueue(&queue);
        }
    };
    const AudioState audio{};
    std::array<std::uint8_t*, 6> addresses{}; // NOLINT(gammaray-raw-pointer-boundary): borrowed synchronous upstream C ABI table.
    for (std::size_t index{}; index < shards.size(); ++index) {
        if (shards[index].size() != shards.front().size())
            return false;
        addresses[index] = shards[index].data();
    }
    std::vector<std::uint8_t> marks(missing.begin(), missing.end());
    return reed_solomon_decode(audio.queue.rs, addresses.data(), marks.data(), 6, static_cast<int>(shards.front().size())) == 0;
}

struct UpstreamVideoOracle::State final {
    std::unique_lock<std::mutex> lock{oracle_mutex, std::try_to_lock};
    RTP_VIDEO_QUEUE queue{};
    std::shared_ptr<OracleResults> results{std::make_shared<OracleResults>()};
    ~State() {
        RtpvCleanupQueue(&queue);
    }
};

UpstreamVideoOracle::UpstreamVideoOracle(std::uint16_t datagram_size) : state_(std::make_unique<State>()) {
    if (!state_->lock.owns_lock() || active_results)
        throw std::logic_error("Only one upstream reference queue may be active");
    StreamConfig.packetSize = datagram_size - MAX_RTP_HEADER_SIZE;
    RtpvInitializeQueue(&state_->queue);
    active_results = state_->results;
}
UpstreamVideoOracle::~UpstreamVideoOracle() {
    active_results.reset();
}

void UpstreamVideoOracle::Feed(const Packet& packet) {
    if (packet.size() < sizeof(RTP_PACKET))
        throw std::invalid_argument("Invalid oracle packet");
    const auto allocation_size = packet.size() + sizeof(RTPV_QUEUE_ENTRY) + alignof(RTPV_QUEUE_ENTRY);
    BufferOwner storage{std::calloc(1, allocation_size)};
    if (!storage)
        throw std::bad_alloc();
    std::memcpy(storage.get(), packet.data(), packet.size());
    // Incoming RTP fields are converted by Moonlight's socket reader before queue insertion.
    auto& rtp = *static_cast<PRTP_PACKET>(storage.get());
    rtp.sequenceNumber = BE16(rtp.sequenceNumber);
    rtp.timestamp = BE32(rtp.timestamp);
    const auto entry_offset = (packet.size() + alignof(RTPV_QUEUE_ENTRY) - 1) & ~(alignof(RTPV_QUEUE_ENTRY) - 1);
    const auto result = RtpvAddPacket(&state_->queue, &rtp, static_cast<int>(packet.size()),
                                      reinterpret_cast<PRTPV_QUEUE_ENTRY>(static_cast<std::uint8_t*>(storage.get()) + entry_offset));
    if (result == RTPF_RET_QUEUED) {
        static_cast<void>(storage.release()); // NOLINT(gammaray-raw-pointer-boundary): successful upstream enqueue owns malloc storage.
    }
}
OracleResults UpstreamVideoOracle::Results() const {
    return *state_->results;
}
} // namespace px::media::test
