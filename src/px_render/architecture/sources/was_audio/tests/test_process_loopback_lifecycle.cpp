#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>

#include <atomic>
#include <cstdio>
#include <memory>

#include "process_loopback_audio_capture.h"

int main() {
    constexpr int kRounds = 10;
    for (int round = 1; round <= kRounds; ++round) {
        const auto capture =
            px::ProcessLoopbackAudioCapture::Make(GetCurrentProcessId());
        const auto format_callbacks = std::make_shared<std::atomic_int>(0);
        const auto stop_callbacks = std::make_shared<std::atomic_int>(0);
        capture->RegisterFormatCallback(
            [format_callbacks](int samples, int channels, int bits) {
                if (samples == 48000 && channels == 2 && bits == 16) {
                    ++(*format_callbacks);
                }
            });
        capture->RegisterStopCallback(
            [stop_callbacks]() { ++(*stop_callbacks); });

        const int start_result = capture->Start();
        if (start_result != 0 || format_callbacks->load() != 1) {
            std::printf(
                "FAIL process-loopback round %d: start=%d format=%d\n",
                round, start_result, format_callbacks->load());
            return 1;
        }
        if (capture->Stop() != 0 || stop_callbacks->load() != 1) {
            std::printf(
                "FAIL process-loopback round %d: stop callbacks=%d\n",
                round, stop_callbacks->load());
            return 2;
        }
        if (capture->Stop() != 0 || stop_callbacks->load() != 1) {
            std::printf(
                "FAIL process-loopback round %d: repeated stop callbacks=%d\n",
                round, stop_callbacks->load());
            return 3;
        }
    }

    std::printf(
        "PASS: process-loopback real activation and idempotent stop %d/%d rounds\n",
        kRounds, kRounds);
    return 0;
}
