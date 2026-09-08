#pragma once

namespace px {

// Native voice wire format, independent of capture/playout devices and APM backends.
struct VoiceAudioFormat final {
    static constexpr int kSampleRate = 48'000;
    static constexpr int kChannels = 1;
    static constexpr int kBitsPerSample = 16;
    static constexpr int kFrameMs = 20;
    static constexpr int kFrameSamples = kSampleRate * kFrameMs / 1000;
    static constexpr int kBitrateBps = 32'000;
};

} // namespace px
