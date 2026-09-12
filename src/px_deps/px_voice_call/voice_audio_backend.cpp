#include "voice_audio_backend.h"

#if !defined(__ANDROID__) && !defined(_WIN32)
#include <cstdlib>
#include <string_view>
#endif

namespace px {

std::unique_ptr<IVoiceAudioBackend> CreateDefaultVoiceAudioBackend() {
#if defined(__ANDROID__)
    return CreateAAudioVoiceAudioBackend();
#elif defined(_WIN32)
    return CreateWasapiVoiceAudioBackend();
#else
    // SDL remains the portable fallback on desktop platforms without a native backend.
    const char* forced = std::getenv("PX_VOICE_AUDIO_BACKEND");
    const char* sdl_driver = std::getenv("SDL_AUDIODRIVER");
    if ((forced && std::string_view(forced) == "sdl") ||
        (sdl_driver && *sdl_driver != '\0')) {
        return CreateSdlVoiceAudioBackend();
    }
    return CreateSdlVoiceAudioBackend();
#endif
}

}  // namespace px
