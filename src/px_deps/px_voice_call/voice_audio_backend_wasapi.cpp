#include "voice_audio_backend.h"

#if defined(_WIN32)

#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>

#include "miniaudio/miniaudio.h"

namespace px {
namespace {

class WasapiVoiceAudioBackend final : public IVoiceAudioBackend {
public:
    ~WasapiVoiceAudioBackend() override { Stop(); }

    bool Start(
        const VoiceAudioBackendConfig& config,
        CaptureCallback capture_callback,
        PlayoutCallback playout_callback,
        EventCallback event_callback,
        std::string* error) override {
        std::scoped_lock lock(lifecycle_mutex_);
        StopLocked();
        if ((!config.capture_enabled && !config.playout_enabled) ||
            (config.capture_enabled && !capture_callback) ||
            (config.playout_enabled && !playout_callback) ||
            config.sample_rate != 48'000 ||
            config.channels != 1 || config.frames_per_callback != 480) {
            SetError(error, "WASAPI voice backend requires 48 kHz mono 10 ms callbacks");
            return false;
        }

        capture_callback_ = std::move(capture_callback);
        playout_callback_ = std::move(playout_callback);
        event_callback_ = std::move(event_callback);

        const ma_backend backends[] = {ma_backend_wasapi};
        const ma_context_config context_config = ma_context_config_init();
        ma_result result = ma_context_init(backends, 1, &context_config, &context_);
        if (result != MA_SUCCESS) {
            SetError(error, ResultError("ma_context_init(WASAPI)", result));
            ClearCallbacks();
            return false;
        }
        context_initialized_ = true;

        const ma_device_type device_type = config.capture_enabled && config.playout_enabled
            ? ma_device_type_duplex
            : (config.capture_enabled ? ma_device_type_capture : ma_device_type_playback);
        ma_device_config device_config = ma_device_config_init(device_type);
        device_config.sampleRate = config.sample_rate;
        device_config.periodSizeInFrames = config.frames_per_callback;
        device_config.periods = 2;
        device_config.performanceProfile = ma_performance_profile_low_latency;
        if (config.capture_enabled) {
            device_config.capture.format = ma_format_s16;
            device_config.capture.channels = config.channels;
            device_config.capture.shareMode = ma_share_mode_shared;
        }
        if (config.playout_enabled) {
            device_config.playback.format = ma_format_s16;
            device_config.playback.channels = config.channels;
            device_config.playback.shareMode = ma_share_mode_shared;
        }
        device_config.wasapi.usage = ma_wasapi_usage_pro_audio;
        device_config.wasapi.noAutoStreamRouting = MA_FALSE;
        device_config.dataCallback = &WasapiVoiceAudioBackend::DataCallback;
        device_config.notificationCallback =
            &WasapiVoiceAudioBackend::NotificationCallback;
        device_config.pUserData = this;

        ma_device_id capture_id{};
        ma_device_id playout_id{};
        if ((config.capture_enabled && !SetDeviceId(config.capture_device_id, &capture_id, error)) ||
            (config.playout_enabled && !SetDeviceId(config.playout_device_id, &playout_id, error))) {
            StopLocked();
            return false;
        }
        if (config.capture_enabled && !config.capture_device_id.empty()) {
            device_config.capture.pDeviceID = &capture_id;
        }
        if (config.playout_enabled && !config.playout_device_id.empty()) {
            device_config.playback.pDeviceID = &playout_id;
        }

        result = ma_device_init(&context_, &device_config, &device_);
        if (result != MA_SUCCESS) {
            const auto playback_probe = config.playout_enabled
                ? ProbeDevice(ma_device_type_playback, config) : "disabled";
            const auto capture_probe = config.capture_enabled
                ? ProbeDevice(ma_device_type_capture, config) : "disabled";
            SetError(error, ResultError("ma_device_init(WASAPI voice)", result) +
                "; playback probe: " + playback_probe +
                "; capture probe: " + capture_probe);
            StopLocked();
            return false;
        }
        device_initialized_ = true;
        info_ = VoiceAudioBackendInfo{
            .backend = "WASAPI shared event-driven (communications)",
            .capture_device = !config.capture_enabled ? "disabled"
                : (device_.capture.name[0] ? device_.capture.name
                                           : "default communications capture"),
            .playout_device = !config.playout_enabled ? "disabled"
                : (device_.playback.name[0] ? device_.playback.name
                                            : "default communications output"),
        };

        stopping_ = false;
        running_ = true;
        result = ma_device_start(&device_);
        if (result != MA_SUCCESS) {
            running_ = false;
            SetError(error, ResultError("ma_device_start(WASAPI voice)", result));
            StopLocked();
            return false;
        }
        return true;
    }

    void Stop() override {
        std::scoped_lock lock(lifecycle_mutex_);
        StopLocked();
    }

    bool EnumerateDevices(
        VoiceAudioDeviceInventory* inventory, std::string* error) override {
        if (!inventory) {
            SetError(error, "audio device inventory is null");
            return false;
        }
        *inventory = {};
        const ma_backend backends[] = {ma_backend_wasapi};
        ma_context context{};
        const ma_context_config context_config = ma_context_config_init();
        ma_result result = ma_context_init(backends, 1, &context_config, &context);
        if (result != MA_SUCCESS) {
            SetError(error, ResultError("ma_context_init(WASAPI enumeration)", result));
            return false;
        }
        ma_device_info* playback = nullptr;
        ma_device_info* capture = nullptr;
        ma_uint32 playback_count = 0;
        ma_uint32 capture_count = 0;
        result = ma_context_get_devices(
            &context, &playback, &playback_count, &capture, &capture_count);
        if (result != MA_SUCCESS) {
            SetError(error, ResultError("ma_context_get_devices(WASAPI)", result));
            ma_context_uninit(&context);
            return false;
        }
        inventory->capture_devices.push_back({
            .id = {}, .name = "Default communications capture", .is_default = true});
        inventory->playout_devices.push_back({
            .id = {}, .name = "Default communications output", .is_default = true});
        for (ma_uint32 i = 0; i < capture_count; ++i) {
            if (const auto id = DeviceIdString(capture[i].id); !id.empty()) {
                inventory->capture_devices.push_back({
                    .id = id,
                    .name = capture[i].name,
                    .is_default = capture[i].isDefault == MA_TRUE,
                });
            }
        }
        for (ma_uint32 i = 0; i < playback_count; ++i) {
            if (const auto id = DeviceIdString(playback[i].id); !id.empty()) {
                inventory->playout_devices.push_back({
                    .id = id,
                    .name = playback[i].name,
                    .is_default = playback[i].isDefault == MA_TRUE,
                });
            }
        }
        ma_context_uninit(&context);
        return true;
    }

    [[nodiscard]] bool IsRunning() const override { return running_; }

    [[nodiscard]] VoiceAudioBackendInfo Info() const override {
        std::scoped_lock lock(lifecycle_mutex_);
        return info_;
    }

private:
    static std::string DeviceIdString(const ma_device_id& id) {
        std::string result;
        for (const auto value : id.wasapi) {
            if (value == 0) {
                break;
            }
            if (value > 0x7f) {
                return {};
            }
            result.push_back(static_cast<char>(value));
        }
        return result;
    }

    std::string ProbeDevice(
        ma_device_type type, const VoiceAudioBackendConfig& config) {
        ma_device_config probe_config = ma_device_config_init(type);
        probe_config.sampleRate = config.sample_rate;
        probe_config.periodSizeInFrames = config.frames_per_callback;
        probe_config.periods = 2;
        probe_config.performanceProfile = ma_performance_profile_low_latency;
        probe_config.wasapi.usage = ma_wasapi_usage_pro_audio;
        probe_config.wasapi.noAutoStreamRouting = MA_FALSE;
        if (type == ma_device_type_playback) {
            probe_config.playback.format = ma_format_s16;
            probe_config.playback.channels = config.channels;
            probe_config.playback.shareMode = ma_share_mode_shared;
            ma_device_id id{};
            if (!SetDeviceId(config.playout_device_id, &id, nullptr)) {
                return "invalid endpoint ID";
            }
            if (!config.playout_device_id.empty()) {
                probe_config.playback.pDeviceID = &id;
            }
            return ProbeConfiguredDevice(type, probe_config);
        }
        else {
            probe_config.capture.format = ma_format_s16;
            probe_config.capture.channels = config.channels;
            probe_config.capture.shareMode = ma_share_mode_shared;
            ma_device_id id{};
            if (!SetDeviceId(config.capture_device_id, &id, nullptr)) {
                return "invalid endpoint ID";
            }
            if (!config.capture_device_id.empty()) {
                probe_config.capture.pDeviceID = &id;
            }
            return ProbeConfiguredDevice(type, probe_config);
        }
    }

    std::string ProbeConfiguredDevice(
        ma_device_type type, const ma_device_config& probe_config) {
        ma_device probe{};
        const ma_result result = ma_device_init(&context_, &probe_config, &probe);
        if (result != MA_SUCCESS) {
            return ResultError(
                type == ma_device_type_playback ? "playback" : "capture", result);
        }
        const std::string name = type == ma_device_type_playback
            ? (probe.playback.name[0] ? probe.playback.name : "default")
            : (probe.capture.name[0] ? probe.capture.name : "default");
        ma_device_uninit(&probe);
        return "ok (" + name + ")";
    }

    static bool SetDeviceId(
        const std::string& value, ma_device_id* destination,
        std::string* error) {
        if (!destination || value.size() >=
                sizeof(destination->wasapi) / sizeof(destination->wasapi[0])) {
            SetError(error, "WASAPI endpoint ID is too long");
            return false;
        }
        for (size_t i = 0; i < value.size(); ++i) {
            const auto byte = static_cast<unsigned char>(value[i]);
            if (byte > 0x7f) {
                SetError(error, "WASAPI endpoint ID must be ASCII");
                return false;
            }
            destination->wasapi[i] = static_cast<ma_wchar_win32>(byte);
        }
        destination->wasapi[value.size()] = 0;
        return true;
    }

    static void DataCallback(
        ma_device* device, void* output, const void* input,
        ma_uint32 frame_count) {
        auto* self = device
            ? static_cast<WasapiVoiceAudioBackend*>(device->pUserData)
            : nullptr;
        if (!self || frame_count == 0) {
            return;
        }
        auto* output_samples = static_cast<int16_t*>(output);
        if (output_samples) {
            std::memset(output_samples, 0, frame_count * sizeof(int16_t));
        }
        if (!self->running_) {
            return;
        }
        if (input && self->capture_callback_) {
            self->capture_callback_(static_cast<const int16_t*>(input), frame_count);
        }
        if (output_samples && self->playout_callback_) {
            self->playout_callback_(output_samples, frame_count);
        }
    }

    static void NotificationCallback(const ma_device_notification* notification) {
        if (!notification || !notification->pDevice) {
            return;
        }
        auto* self = static_cast<WasapiVoiceAudioBackend*>(
            notification->pDevice->pUserData);
        if (!self || self->stopping_ || !self->event_callback_) {
            return;
        }
        switch (notification->type) {
        case ma_device_notification_type_rerouted:
            self->event_callback_(VoiceAudioBackendEvent::kRerouted, "rerouted");
            break;
        case ma_device_notification_type_interruption_began:
            self->event_callback_(
                VoiceAudioBackendEvent::kInterruptionBegan, "interruption_began");
            break;
        case ma_device_notification_type_interruption_ended:
            self->event_callback_(
                VoiceAudioBackendEvent::kInterruptionEnded, "interruption_ended");
            break;
        case ma_device_notification_type_stopped:
            self->event_callback_(VoiceAudioBackendEvent::kStopped, "device_stopped");
            break;
        default:
            break;
        }
    }

    void StopLocked() {
        stopping_ = true;
        running_ = false;
        if (device_initialized_) {
            ma_device_stop(&device_);
            ma_device_uninit(&device_);
            device_initialized_ = false;
        }
        if (context_initialized_) {
            ma_context_uninit(&context_);
            context_initialized_ = false;
        }
        info_ = {};
        ClearCallbacks();
        stopping_ = false;
    }

    void ClearCallbacks() {
        capture_callback_ = {};
        playout_callback_ = {};
        event_callback_ = {};
    }

    static std::string ResultError(const char* operation, ma_result result) {
        const char* description = ma_result_description(result);
        return std::string(operation) + " failed: " +
            (description ? description : "unknown") +
            " (" + std::to_string(static_cast<int>(result)) + ")";
    }

    static void SetError(std::string* error, std::string value) {
        if (error) {
            *error = std::move(value);
        }
    }

    mutable std::mutex lifecycle_mutex_;
    ma_context context_{};
    ma_device device_{};
    bool context_initialized_ = false;
    bool device_initialized_ = false;
    std::atomic_bool running_ = false;
    std::atomic_bool stopping_ = false;
    CaptureCallback capture_callback_;
    PlayoutCallback playout_callback_;
    EventCallback event_callback_;
    VoiceAudioBackendInfo info_;
};

}  // namespace

std::unique_ptr<IVoiceAudioBackend> CreateWasapiVoiceAudioBackend() {
    return std::make_unique<WasapiVoiceAudioBackend>();
}

}  // namespace px

#endif  // defined(_WIN32)
