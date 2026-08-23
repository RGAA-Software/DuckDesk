#include "voice_audio_backend.h"

#include <cstdlib>
#include <string_view>

namespace px {

std::unique_ptr<IVoiceAudioBackend> CreateDefaultVoiceAudioBackend() {
    const char* forced = std::getenv("PX_VOICE_AUDIO_BACKEND");
    const char* sdl_driver = std::getenv("SDL_AUDIODRIVER");
    if ((forced && std::string_view(forced) == "sdl") ||
        (sdl_driver && *sdl_driver != '\0')) {
        return CreateSdlVoiceAudioBackend();
    }
#if defined(_WIN32)
    return CreateWasapiVoiceAudioBackend();
#else
    return CreateSdlVoiceAudioBackend();
#endif
}

}  // namespace px
