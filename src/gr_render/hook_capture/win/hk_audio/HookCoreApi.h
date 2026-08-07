#pragma once

#include <audioclient.h>
#include <mmdeviceapi.h>

#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#include "AudioMixer.h"
#include "SimpleAudioFormatConverter.h"

namespace tc {

class AudioShare;

// enable_hook_audio=1: hook common Windows playback APIs and mix into AudioShare.
// Covers multi-device / multi-instance init (BGM + SFX often use separate clients).
class HookCoreApi {
public:
    struct StreamFormat {
        SimpleAudioFormat format = SimpleAudioFormat::kPCM_S16;
        int samples = 48000;
        int channels = 2;
        int bits = 16;
        int block_align = 4;
    };

    static HookCoreApi* Instance() {
        static HookCoreApi api;
        return &api;
    }

    bool Init();
    void Shutdown();

    std::shared_ptr<AudioShare> audio_share;
    std::shared_ptr<AudioMixer> mixer;

    std::mutex state_mu_;
    std::unordered_map<IAudioClient*, StreamFormat> client_formats_;
    std::unordered_map<IAudioRenderClient*, StreamFormat> render_formats_;
    std::unordered_map<IAudioRenderClient*, char*> render_buffers_;
    std::unordered_set<void*> patched_render_vtbls_;

    void PatchRenderClientVtable(IAudioRenderClient* rc);

private:
    HookCoreApi() = default;
    bool InitWasapiRenderHooks();

    bool hooked_ = false;
};

}  // namespace tc
