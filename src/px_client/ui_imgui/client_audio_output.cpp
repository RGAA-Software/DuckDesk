#include "client_audio_output.h"

#include <SDL3/SDL.h>

#include <mutex>
#include <string>

#include "px_common/data.h"
#include "px_common/log.h"

namespace px::client::imgui {

struct ClientAudioOutput::Impl final {
    struct AudioSubsystem final {
        AudioSubsystem() = default;
        AudioSubsystem(const AudioSubsystem&) = delete;
        AudioSubsystem& operator=(const AudioSubsystem&) = delete;
        bool initialized{};

        ~AudioSubsystem() {
            if (initialized) SDL_QuitSubSystem(SDL_INIT_AUDIO);
        }

        bool Start() {
            if (initialized) return true;
            initialized = SDL_InitSubSystem(SDL_INIT_AUDIO);
            return initialized;
        }
    };

    struct AudioStreamDeleter final {
        void operator()(SDL_AudioStream* stream) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): SDL-owned stream deleter ABI.
            if (stream != nullptr) {
                SDL_DestroyAudioStream(stream);
            }
        }
    };

    std::mutex mutex{};
    AudioSubsystem subsystem{};
    std::unique_ptr<SDL_AudioStream, AudioStreamDeleter> stream{};
    int sampleRate{};
    int channels{};
    bool wrotePcm{};
    std::string lastDeviceError{};
};

ClientAudioOutput::ClientAudioOutput() : impl_{std::make_unique<Impl>()} {}
ClientAudioOutput::~ClientAudioOutput() { Stop(); }

bool ClientAudioOutput::Start() {
    const std::scoped_lock lock{impl_->mutex};
    if (impl_->subsystem.initialized) return true;
    if (!impl_->subsystem.Start()) {
        LOGE("event=client.audio_output phase=initialize outcome=failed reason={}", SDL_GetError());
        return false;
    }
    LOGI("event=client.audio_output phase=initialize outcome=success driver={}", SDL_GetCurrentAudioDriver());
    return true;
}

bool ClientAudioOutput::Write(const std::shared_ptr<px::Data>& pcmData, const int sampleRate, const int channels, const int bitsPerSample) {
    if (!pcmData || pcmData->Size() <= 0 || sampleRate <= 0 || channels <= 0 || bitsPerSample != 16) {
        return false;
    }
    const std::scoped_lock lock{impl_->mutex};
    if (!impl_->subsystem.initialized) return false;
    if (!impl_->stream || impl_->sampleRate != sampleRate || impl_->channels != channels) {
        impl_->stream.reset();
        const SDL_AudioSpec specification{.format = SDL_AUDIO_S16, .channels = channels, .freq = sampleRate};
        impl_->stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &specification, nullptr, nullptr));
        if (!impl_->stream || !SDL_ResumeAudioStreamDevice(impl_->stream.get())) {
            const std::string deviceError{SDL_GetError()};
            if (deviceError != impl_->lastDeviceError) {
                LOGE("event=client.audio_output phase=open outcome=failed reason={}", deviceError);
                impl_->lastDeviceError = deviceError;
            }
            impl_->stream.reset();
            return false;
        }
        impl_->lastDeviceError.clear();
        impl_->wrotePcm = false;
        impl_->sampleRate = sampleRate;
        impl_->channels = channels;
        LOGI("event=client.audio_output phase=open outcome=success sample_rate={} channels={}", sampleRate, channels);
    }
    const bool accepted{SDL_PutAudioStreamData(impl_->stream.get(), pcmData->Bytes().data(), pcmData->Size())};
    if (!accepted) {
        const std::string deviceError{SDL_GetError()};
        if (deviceError != impl_->lastDeviceError) {
            LOGE("event=client.audio_output phase=write outcome=failed reason={}", deviceError);
            impl_->lastDeviceError = deviceError;
        }
    } else if (!impl_->wrotePcm) {
        impl_->wrotePcm = true;
        LOGI("event=client.audio_output phase=first_write outcome=success bytes={}", pcmData->Size());
    }
    return accepted;
}

void ClientAudioOutput::Stop() {
    const std::scoped_lock lock{impl_->mutex};
    impl_->stream.reset();
    impl_->sampleRate = 0;
    impl_->channels = 0;
    impl_->wrotePcm = false;
}

}  // namespace px::client::imgui
