#include <chrono>
#include <cstdio>
#include <memory>

#include "miniaudio_audio_capture.h"

int main() {
    for (int round = 0; round < 10; ++round) {
        auto capture = px::MiniAudioCapture::Make();
        const auto mini = std::dynamic_pointer_cast<px::MiniAudioCapture>(capture);
        if (!mini || capture->Start() != 0) {
            std::printf("FAIL: cancellation round %d failed to start\n", round + 1);
            return 1;
        }

        mini->RequestReinitForTesting("stop-during-delay");
        const auto stop_started = std::chrono::steady_clock::now();
        capture->Stop();
        const auto stop_elapsed = std::chrono::steady_clock::now() - stop_started;
        if (stop_elapsed >= std::chrono::milliseconds(500) ||
            mini->SuccessfulReinitCountForTesting() != 0) {
            std::printf("FAIL: cancellation round %d did not cancel promptly\n",
                        round + 1);
            return 2;
        }
    }

    std::printf("PASS: 10/10 pending reinit cancellation rounds\n");
    return 0;
}
