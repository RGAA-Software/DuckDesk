//
// Created by RGAA on 7/12/2024.
//

#include "px_audio_encoder_plugin.h"

namespace px
{

    PxAudioEncoderPlugin::PxAudioEncoderPlugin() {

    }

    PxAudioEncoderPlugin::~PxAudioEncoderPlugin() {

    }

    bool PxAudioEncoderPlugin::OnCreate(const px::PxPluginParam& param) {
        PxPluginInterface::OnCreate(param);

        return true;
    }

    bool PxAudioEncoderPlugin::OnDestroy() {
        return PxPluginInterface::OnDestroy();
    }

    void PxAudioEncoderPlugin::Encode(const std::shared_ptr<Data>& data, int sample, int channels, int bits) {

    }

}
