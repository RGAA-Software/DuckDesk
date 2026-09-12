#include "client_audio_output.h"

#include "px_common/data.h"

#include <SDL3/SDL.h>

#include <mutex>

namespace px::client::imgui {

struct ClientAudioOutput::Impl final {
    struct AudioStreamDeleter final {
        void operator()(SDL_AudioStream* stream) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): SDL-owned stream deleter ABI.
            if (stream != nullptr) {
                SDL_DestroyAudioStream(stream);
            }
        }
    };

    std::mutex mutex{};
    std::unique_ptr<SDL_AudioStream, AudioStreamDeleter> stream{};
    int sampleRate{};
    int channels{};
};

ClientAudioOutput::ClientAudioOutput() : impl_{std::make_unique<Impl>()} {}
ClientAudioOutput::~ClientAudioOutput() {
    Stop();
}

bool ClientAudioOutput::Write(const std::shared_ptr<px::Data>& data, const int sampleRate, const int channels, const int bitsPerSample) {
    if (!data || data->Size() <= 0 || sampleRate <= 0 || channels <= 0 || bitsPerSample != 16) {
        return false;
    }
    const std::scoped_lock lock{impl_->mutex};
    if (!impl_->stream || impl_->sampleRate != sampleRate || impl_->channels != channels) {
        impl_->stream.reset();
        const SDL_AudioSpec specification{.format = SDL_AUDIO_S16, .channels = channels, .freq = sampleRate};
        impl_->stream.reset(SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &specification, nullptr, nullptr));
        if (!impl_->stream || !SDL_ResumeAudioStreamDevice(impl_->stream.get())) {
            impl_->stream.reset();
            return false;
        }
        impl_->sampleRate = sampleRate;
        impl_->channels = channels;
    }
    return SDL_PutAudioStreamData(impl_->stream.get(), data->Bytes().data(), data->Size());
}

void ClientAudioOutput::Stop() {
    const std::scoped_lock lock{impl_->mutex};
    impl_->stream.reset();
    impl_->sampleRate = 0;
    impl_->channels = 0;
}

} // namespace px::client::imgui
