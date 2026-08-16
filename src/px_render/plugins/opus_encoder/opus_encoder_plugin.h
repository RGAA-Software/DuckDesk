//
// Created by RGAA on 15/11/2024.
//

#ifndef PX_RENDER_OPUS_ENCODER_PLUGIN_H
#define PX_RENDER_OPUS_ENCODER_PLUGIN_H

#include "px_render/plugin_interface/px_audio_encoder_plugin.h"

namespace px
{

    class Data;
    class OpusAudioEncoder;
    class OpusAudioDecoder;

    class OpusEncoderPlugin : public PxAudioEncoderPlugin {
    public:
        std::string GetPluginId() override;
        std::string GetPluginName() override;
        std::string GetVersionName() override;
        uint32_t GetVersionCode() override;
        std::string GetPluginDescription() override;
        void On1Second() override;

        bool OnCreate(const px::PxPluginParam &param) override;
        bool OnDestroy() override;
        void Encode(const std::shared_ptr<Data> &data, int sample, int channels, int bits) override;

    private:
        std::shared_ptr<OpusAudioEncoder> opus_encoder_ = nullptr;
        bool debug_opus_decoder_ = false;
        std::shared_ptr<OpusAudioDecoder> opus_decoder_ = nullptr;

        std::shared_ptr<Data> audio_cache_ = nullptr;
        int audio_callback_count_ = 0;

    };

}


PX_PLUGIN_EXPORT(px::OpusEncoderPlugin)


#endif //PX_UDP_PLUGIN_H
