#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "AudioMixer.h"
#include "AudioShare.h"
#include "px_common_new/data.h"

namespace {

using namespace std::chrono_literals;

std::shared_ptr<px::Data> MakePcmPacket(size_t frames = 960) {
    const std::vector<int16_t> samples(frames * 2, 2048);
    return px::Data::Make(reinterpret_cast<const char*>(samples.data()),
                          static_cast<int>(samples.size() * sizeof(int16_t)));
}

bool WaitFor(const std::shared_ptr<std::atomic<int>>& value, int expected) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (value->load(std::memory_order_acquire) >= expected) {
            return true;
        }
        std::this_thread::yield();
    }
    return value->load(std::memory_order_acquire) >= expected;
}

bool TestQueuedDrain() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto share = std::make_shared<px::AudioShare>();
    share->SetIpcSender([calls](std::string&&) {
        calls->fetch_add(1, std::memory_order_release);
    });
    share->PostAudioData(MakePcmPacket());
    share->PostAudioData(MakePcmPacket());
    share->Stop();
    return calls->load(std::memory_order_acquire) == 2;
}

bool TestUnregisterDuringDispatch() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto share = std::make_shared<px::AudioShare>();
    const std::weak_ptr<px::AudioShare> weak_share = share;
    share->SetIpcSender([calls, weak_share](std::string&&) {
        calls->fetch_add(1, std::memory_order_release);
        if (const auto locked = weak_share.lock()) {
            locked->SetIpcSender({});
        }
    });
    for (int index = 0; index < 8; ++index) {
        share->PostAudioData(MakePcmPacket());
    }
    share->Stop();
    return calls->load(std::memory_order_acquire) == 1;
}

bool TestDestroyFromCallback() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto owner = std::make_shared<std::shared_ptr<px::AudioShare>>(
        std::make_shared<px::AudioShare>());
    (*owner)->SetIpcSender([calls, owner](std::string&&) {
        calls->fetch_add(1, std::memory_order_release);
        owner->reset();
    });
    (*owner)->PostAudioData(MakePcmPacket());
    return WaitFor(calls, 1) && !*owner;
}

bool TestMixerStopAndLatePush() {
    auto calls = std::make_shared<std::atomic<int>>(0);
    auto share = std::make_shared<px::AudioShare>();
    share->SetIpcSender([calls](std::string&&) {
        calls->fetch_add(1, std::memory_order_release);
    });
    auto mixer = std::make_shared<px::AudioMixer>(share);
    const std::vector<int16_t> samples(960 * 2, 4096);
    const auto bytes = std::as_bytes(std::span(samples));
    mixer->Push(bytes, px::SimpleAudioFormat::kPCM_S16, 48000, 2, "lifecycle");
    mixer->Stop();
    const auto pushed_before = mixer->pushed();
    mixer->Push(bytes, px::SimpleAudioFormat::kPCM_S16, 48000, 2, "late");
    share->Stop();
    return pushed_before == 1 && mixer->pushed() == pushed_before &&
           calls->load(std::memory_order_acquire) == 1;
}

}  // namespace

int main() {
    constexpr int kRounds = 10;
    for (int round = 1; round <= kRounds; ++round) {
        if (!TestQueuedDrain() || !TestUnregisterDuringDispatch() ||
            !TestDestroyFromCallback() || !TestMixerStopAndLatePush()) {
            std::cerr << "hook audio worker lifecycle failed at round " << round << '\n';
            return 1;
        }
        std::cout << "hook audio worker lifecycle round " << round << "/" << kRounds
                  << " passed\n";
    }
    return 0;
}
