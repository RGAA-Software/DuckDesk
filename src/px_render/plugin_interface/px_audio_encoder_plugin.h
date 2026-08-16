//
// Created by RGAA on 7/12/2024.
//

#ifndef PX_AUDIO_ENCODER_PLUGIN_H
#define PX_AUDIO_ENCODER_PLUGIN_H

#include "px_plugin_interface.h"

namespace px
{

    class Data;

    class PxAudioEncoderPlugin : public PxPluginInterface {
    public:
        PxAudioEncoderPlugin();
        ~PxAudioEncoderPlugin() override;

        bool OnCreate(const px::PxPluginParam& param) override;
        bool OnDestroy() override;

        virtual void Encode(const std::shared_ptr<Data>& data, int sample, int channels, int bits);

    };

}

#endif //PX_AUDIO_ENCODER_PLUGIN_H
