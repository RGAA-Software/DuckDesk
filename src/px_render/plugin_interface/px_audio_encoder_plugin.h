//
// Created by RGAA on 7/12/2024.
//

#ifndef GAMMARAY_GR_AUDIO_ENCODER_PLUGIN_H
#define GAMMARAY_GR_AUDIO_ENCODER_PLUGIN_H

#include "px_plugin_interface.h"

namespace px
{

    class Data;

    class GrAudioEncoderPlugin : public GrPluginInterface {
    public:
        GrAudioEncoderPlugin();
        ~GrAudioEncoderPlugin() override;

        bool OnCreate(const px::GrPluginParam& param) override;
        bool OnDestroy() override;

        virtual void Encode(const std::shared_ptr<Data>& data, int sample, int channels, int bits);

    };

}

#endif //GAMMARAY_GR_AUDIO_ENCODER_PLUGIN_H
