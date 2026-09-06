#pragma once

#include <cstdint>
#include <memory>

namespace px {
class Data;
}

namespace pixels::android {

class NativeAudioPlayer final {
  public:
    NativeAudioPlayer();
    ~NativeAudioPlayer();

    NativeAudioPlayer(const NativeAudioPlayer&) = delete;
    NativeAudioPlayer& operator=(const NativeAudioPlayer&) = delete;

    bool Write(const std::shared_ptr<px::Data>& pcm, std::int32_t sample_rate, std::int32_t channels, std::int32_t bits_per_sample);
    void SetEnabled(bool enabled);
    void Stop();

  private:
    class Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace pixels::android
