//
// Created by RGAA on 2023/12/22.
//

#ifndef TC_APPLICATION_AUDIO_CAPTURE_FACTORY_H
#define TC_APPLICATION_AUDIO_CAPTURE_FACTORY_H

#include <memory>

#include "audio_capture.h"

#ifdef WIN32
#include "win/audio/wasapi_audio_capture.h"
#endif

namespace tc
{

    class AudioCaptureFactory {
    public:

        static std::shared_ptr<IAudioCapture> Make(const std::string& device_id) {
#ifdef WIN32
            return WASAPIAudioCapture::Make(device_id);
#endif
            return nullptr;
        }

    };

}

#endif //TC_APPLICATION_AUDIO_CAPTURE_FACTORY_H
