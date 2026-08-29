#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace px {

struct VoiceAudioBackendConfig {
    uint32_t sample_rate = 48'000;
    uint32_t channels = 1;
    uint32_t frames_per_callback = 480;
    bool capture_enabled = true;
    bool playout_enabled = true;
    // Empty means the platform default communications endpoint. WASAPI uses
    // its stable endpoint ID; SDL uses the exact enumerated device name.
    std::string capture_device_id;
    std::string playout_device_id;
};

struct VoiceAudioBackendInfo {
    std::string backend;
    std::string capture_device;
    std::string playout_device;
};

struct VoiceAudioDeviceDescriptor {
    std::string id;
    std::string name;
    bool is_default = false;
};

struct VoiceAudioDeviceInventory {
    std::vector<VoiceAudioDeviceDescriptor> capture_devices;
    std::vector<VoiceAudioDeviceDescriptor> playout_devices;
};

enum class VoiceAudioBackendEvent {
    kRerouted,
    kInterruptionBegan,
    kInterruptionEnded,
    kStopped,
};

class IVoiceAudioBackend {
public:
    using CaptureCallback =
        std::function<void(std::span<const int16_t> samples)>;
    using PlayoutCallback =
        std::function<void(std::span<int16_t> samples)>;
    using EventCallback =
        std::function<void(VoiceAudioBackendEvent, const std::string&)>;

    virtual ~IVoiceAudioBackend() = default;
    virtual bool Start(
        const VoiceAudioBackendConfig& config,
        CaptureCallback capture_callback,
        PlayoutCallback playout_callback,
        EventCallback event_callback,
        std::string& error) = 0;
    virtual void Stop() = 0;
    virtual bool EnumerateDevices(
        VoiceAudioDeviceInventory& inventory, std::string& error) = 0;
    [[nodiscard]] virtual bool IsRunning() const = 0;
    [[nodiscard]] virtual VoiceAudioBackendInfo Info() const = 0;
};

std::unique_ptr<IVoiceAudioBackend> CreateDefaultVoiceAudioBackend();
std::unique_ptr<IVoiceAudioBackend> CreateSdlVoiceAudioBackend();
#if defined(_WIN32)
std::unique_ptr<IVoiceAudioBackend> CreateWasapiVoiceAudioBackend();
#endif

}  // namespace px
