#pragma once

#include <cstddef>
#include <cstdint>

namespace px {

struct VoiceAudioStats {
    uint64_t captured_frames = 0;
    uint64_t encoded_packets = 0;
    uint64_t decoded_packets = 0;
    // Decoded WebRTC PCM samples accepted by the physical playout path.
    // Kept separate from decoded_packets, which is the legacy Opus path.
    uint64_t received_pcm_samples = 0;
    uint64_t capture_samples_dropped = 0;
    uint64_t playout_samples_dropped = 0;
    uint64_t playout_underruns = 0;
    uint64_t plc_packets = 0;
    uint64_t jitter_duplicates = 0;
    uint64_t jitter_late = 0;
    uint64_t jitter_invalid = 0;
    uint64_t jitter_overflow_drops = 0;
    uint64_t jitter_missing = 0;
    uint64_t apm_capture_frames = 0;
    uint64_t apm_render_frames = 0;
    uint64_t apm_capture_failures = 0;
    uint64_t apm_render_failures = 0;
    size_t jitter_queued_packets = 0;
    size_t jitter_peak_packets = 0;
    uint64_t device_rebuilds = 0;
    uint64_t device_failures = 0;
};

} // namespace px
