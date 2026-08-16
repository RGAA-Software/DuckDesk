//
// Created RGAA on 15/11/2024.
//

#include "opus_encoder_plugin.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_opus_codec_new/opus_codec.h"
#include "px_common_new/log.h"
#include "px_common_new/data.h"
#include "px_common_new/file.h"
#include "px_common_new/time_util.h"
#include "px_common_new/memory_stat.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugin_interface/px_plugin_context.h"

namespace px
{
    std::string OpusEncoderPlugin::GetPluginId() {
        return kOpusEncoderPluginId;
    }

    std::string OpusEncoderPlugin::GetPluginName() {
        return "OPUS Encoder";
    }

    std::string OpusEncoderPlugin::GetVersionName() {
        return "1.1.0";
    }

    uint32_t OpusEncoderPlugin::GetVersionCode() {
        return 110;
    }

    std::string OpusEncoderPlugin::GetPluginDescription() {
        return "OPUS audio encoder";
    }

    void OpusEncoderPlugin::On1Second() {
#if MEMORY_STST_ON
        plugin_context_->PostWorkTask([=, this]() {
            auto info = MemoryStat::Instance()->GetStatInfo();
            LOGI("Memory usage: {}", info.Dump());
        });
#endif
    }

    bool OpusEncoderPlugin::OnCreate(const px::PxPluginParam &param) {
        PxAudioEncoderPlugin::OnCreate(param);
        auto key_save_debug_file = "save_debug_file";
        if (HasParam(key_save_debug_file)) {
            debug_opus_decoder_ = GetConfigParam<bool>(key_save_debug_file);
        }

        return true;
    }

    bool OpusEncoderPlugin::OnDestroy() {
        PxAudioEncoderPlugin::OnDestroy();
        return true;
    }

    void OpusEncoderPlugin::Encode(const std::shared_ptr<Data> &data, int samples, int channels, int bits) {
        if (!opus_encoder_) {
            // audio cache
            audio_cache_ = Data::Make(nullptr, 1024*16);
            LOGI("audio format, samples: {}, channels: {}, bits: {}", samples, channels, bits);
            // UDP 音频无重传,靠 Opus inband FEC 抗丢包;15% 的预期丢包率在局域网偏高,
            // 换取丢 1~2 个 20ms 音频包时仍能恢复,而不是立刻 PLC。
            opus_encoder_ = std::make_shared<OpusAudioEncoder>(samples, channels, bits, OPUS_APPLICATION_AUDIO, 15);
            if (!opus_encoder_->valid()) {
                opus_encoder_ = nullptr;
                return;
            }
            opus_encoder_->SetComplexity(8);
        }

        if (debug_opus_decoder_) {
            static auto pcm_file = File::OpenForWriteB(U8Path("1.opus.encoder.plugin.origin.pcm"));
            pcm_file->Append((char*)data->DataAddr(), data->Size());
        }

        PostWorkTask([=, this]() {
            if (IsStoppingOrDestroyed() || !opus_encoder_ || !audio_cache_) {
                return;
            }
            audio_cache_->Append(data->DataAddr(), data->Size());
            // 2 or 6
            if (++audio_callback_count_ < 2) {
                return;
            }

            //int frame_size = data->Size() / 2 / 2;
            int frame_size = audio_cache_->Offset()/2/2;
            auto encoded_frames = opus_encoder_->Encode(audio_cache_->CStr(), audio_cache_->Offset(), frame_size);
            for (const auto& ef : encoded_frames) {
                auto encoded_data = Data::Make((char*)ef.data(), ef.size());

                auto event = std::make_shared<PxPluginEncodedAudioFrameEvent>();
                event->sample_rate_ = samples;
                event->channels_ = channels;
                event->bits_ = bits;
                event->frame_size_ = frame_size;
                event->data_ = encoded_data;
                CallbackEventDirectly(event);

                if (debug_opus_decoder_) {
                    if (!opus_decoder_) {
                        opus_decoder_ = std::make_shared<OpusAudioDecoder>(opus_encoder_->SampleRate(), opus_encoder_->Channels());
                    }
                    std::vector<unsigned char> buffer(ef.begin(), ef.end());
                    auto pcm_data = opus_decoder_->Decode(buffer, frame_size, false);
                    static auto pcm_file = File::OpenForWriteB(U8Path("1.test.pcm"));
                    pcm_file->Append((char*)pcm_data.data(), pcm_data.size()*2);
                }
            }

            audio_cache_->Reset();
            audio_callback_count_ = 0;

        });
    }

}
