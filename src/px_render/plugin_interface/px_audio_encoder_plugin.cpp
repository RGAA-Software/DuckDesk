//
// Created by RGAA on 7/12/2024.
//

#include "px_audio_encoder_plugin.h"

namespace px
{

    GrAudioEncoderPlugin::GrAudioEncoderPlugin() {

    }

    GrAudioEncoderPlugin::~GrAudioEncoderPlugin() {

    }

    bool GrAudioEncoderPlugin::OnCreate(const px::GrPluginParam& param) {
        GrPluginInterface::OnCreate(param);

        return true;
    }

    bool GrAudioEncoderPlugin::OnDestroy() {
        return GrPluginInterface::OnDestroy();
    }

    void GrAudioEncoderPlugin::Encode(const std::shared_ptr<Data>& data, int sample, int channels, int bits) {

    }

}
