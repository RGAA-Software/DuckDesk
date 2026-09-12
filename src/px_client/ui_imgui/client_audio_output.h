#pragma once

#include <cstdint>
#include <memory>

namespace px {
class Data;
}

namespace px::client::imgui {

class ClientAudioOutput final {
  public:
    ClientAudioOutput();
    ~ClientAudioOutput();
    ClientAudioOutput(const ClientAudioOutput&) = delete;
    ClientAudioOutput& operator=(const ClientAudioOutput&) = delete;

    bool Write(const std::shared_ptr<px::Data>& data, int sampleRate, int channels, int bitsPerSample);
    void Stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

} // namespace px::client::imgui
