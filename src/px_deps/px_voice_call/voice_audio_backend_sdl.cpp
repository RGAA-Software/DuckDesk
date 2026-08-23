#include "voice_audio_backend.h"

#include <SDL2/SDL.h>

#include <atomic>
#include <cstring>
#include <utility>

namespace px {
namespace {

class SdlVoiceAudioBackend final : public IVoiceAudioBackend {
public:
    ~SdlVoiceAudioBackend() override { Stop(); }

    bool Start(
        const VoiceAudioBackendConfig& config,
        CaptureCallback capture_callback,
        PlayoutCallback playout_callback,
        EventCallback event_callback,
        std::string* error) override {
        Stop();
        if ((!config.capture_enabled && !config.playout_enabled) ||
            (config.capture_enabled && !capture_callback) ||
            (config.playout_enabled && !playout_callback) || config.channels != 1 ||
            config.frames_per_callback == 0 || config.frames_per_callback > 65'535) {
            SetError(error, "invalid SDL voice backend configuration");
            return false;
        }
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SetError(error, std::string("SDL audio initialization failed: ") + SDL_GetError());
            return false;
        }
        capture_callback_ = std::move(capture_callback);
        playout_callback_ = std::move(playout_callback);
        event_callback_ = std::move(event_callback);

        SDL_AudioSpec desired{};
        desired.freq = static_cast<int>(config.sample_rate);
        desired.format = AUDIO_S16SYS;
        desired.channels = static_cast<Uint8>(config.channels);
        desired.samples = static_cast<Uint16>(config.frames_per_callback);
        desired.userdata = this;
        capture_device_name_ = config.capture_device_id;
        playout_device_name_ = config.playout_device_id;
        if (config.capture_enabled) {
            desired.callback = &SdlVoiceAudioBackend::CaptureCallback;
            capture_device_ = SDL_OpenAudioDevice(
                capture_device_name_.empty() ? nullptr : capture_device_name_.c_str(),
                SDL_TRUE, &desired, nullptr, 0);
            if (capture_device_ == 0) {
                SetError(error, std::string("default microphone unavailable: ") + SDL_GetError());
                ClearCallbacks();
                return false;
            }
        }

        if (config.playout_enabled) {
            desired.callback = &SdlVoiceAudioBackend::PlaybackCallback;
            playout_device_ = SDL_OpenAudioDevice(
                playout_device_name_.empty() ? nullptr : playout_device_name_.c_str(),
                SDL_FALSE, &desired, nullptr, 0);
            if (playout_device_ == 0) {
                SetError(error, std::string("default output unavailable: ") + SDL_GetError());
                if (capture_device_ != 0) {
                    SDL_CloseAudioDevice(capture_device_);
                    capture_device_ = 0;
                }
                ClearCallbacks();
                return false;
            }
        }
        running_ = true;
        if (playout_device_ != 0) SDL_PauseAudioDevice(playout_device_, 0);
        if (capture_device_ != 0) SDL_PauseAudioDevice(capture_device_, 0);
        return true;
    }

    void Stop() override {
        running_ = false;
        if (capture_device_ != 0) {
            SDL_PauseAudioDevice(capture_device_, 1);
            SDL_CloseAudioDevice(capture_device_);
            capture_device_ = 0;
        }
        if (playout_device_ != 0) {
            SDL_PauseAudioDevice(playout_device_, 1);
            SDL_CloseAudioDevice(playout_device_);
            playout_device_ = 0;
        }
        ClearCallbacks();
    }

    bool EnumerateDevices(
        VoiceAudioDeviceInventory* inventory, std::string* error) override {
        if (!inventory) {
            SetError(error, "audio device inventory is null");
            return false;
        }
        *inventory = {};
        if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
            SetError(error, std::string("SDL audio initialization failed: ") + SDL_GetError());
            return false;
        }
        inventory->capture_devices.push_back({
            .id = {}, .name = "Default capture device", .is_default = true});
        inventory->playout_devices.push_back({
            .id = {}, .name = "Default playout device", .is_default = true});
        const int capture_count = SDL_GetNumAudioDevices(SDL_TRUE);
        const int playout_count = SDL_GetNumAudioDevices(SDL_FALSE);
        if (capture_count < 0 || playout_count < 0) {
            SetError(error, std::string("SDL audio enumeration failed: ") + SDL_GetError());
            return false;
        }
        for (int i = 0; i < capture_count; ++i) {
            if (const char* name = SDL_GetAudioDeviceName(i, SDL_TRUE); name && *name) {
                inventory->capture_devices.push_back({.id = name, .name = name});
            }
        }
        for (int i = 0; i < playout_count; ++i) {
            if (const char* name = SDL_GetAudioDeviceName(i, SDL_FALSE); name && *name) {
                inventory->playout_devices.push_back({.id = name, .name = name});
            }
        }
        return true;
    }

    [[nodiscard]] bool IsRunning() const override { return running_; }

    [[nodiscard]] VoiceAudioBackendInfo Info() const override {
        return VoiceAudioBackendInfo{
            .backend = "SDL",
            .capture_device = capture_device_name_.empty() ? "default" : capture_device_name_,
            .playout_device = playout_device_name_.empty() ? "default" : playout_device_name_,
        };
    }

private:
    static void SetError(std::string* error, std::string value) {
        if (error) {
            *error = std::move(value);
        }
    }

    static void CaptureCallback(void* userdata, Uint8* stream, int length) {
        auto* self = static_cast<SdlVoiceAudioBackend*>(userdata);
        if (!self || !self->running_ || !self->capture_callback_ || length <= 0) {
            return;
        }
        self->capture_callback_(
            reinterpret_cast<const int16_t*>(stream),
            static_cast<size_t>(length) / sizeof(int16_t));
    }

    static void PlaybackCallback(void* userdata, Uint8* stream, int length) {
        auto* self = static_cast<SdlVoiceAudioBackend*>(userdata);
        if (!self || length <= 0) {
            return;
        }
        const auto samples = static_cast<size_t>(length) / sizeof(int16_t);
        std::memset(stream, 0, static_cast<size_t>(length));
        if (self->running_ && self->playout_callback_) {
            self->playout_callback_(reinterpret_cast<int16_t*>(stream), samples);
        }
    }

    void ClearCallbacks() {
        capture_callback_ = {};
        playout_callback_ = {};
        event_callback_ = {};
        capture_device_name_.clear();
        playout_device_name_.clear();
    }

    IVoiceAudioBackend::CaptureCallback capture_callback_;
    IVoiceAudioBackend::PlayoutCallback playout_callback_;
    IVoiceAudioBackend::EventCallback event_callback_;
    std::string capture_device_name_;
    std::string playout_device_name_;
    SDL_AudioDeviceID capture_device_ = 0;
    SDL_AudioDeviceID playout_device_ = 0;
    std::atomic_bool running_ = false;
};

}  // namespace

std::unique_ptr<IVoiceAudioBackend> CreateSdlVoiceAudioBackend() {
    return std::make_unique<SdlVoiceAudioBackend>();
}

}  // namespace px
