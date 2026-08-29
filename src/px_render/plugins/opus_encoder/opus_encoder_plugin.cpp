#include "opus_encoder_plugin.h"

#include "opus_encoder_runtime.h"
#include "px_common_new/log.h"
#include "px_common_new/memory_stat.h"
#include "px_render/plugin_interface/px_plugin_context.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {

OpusEncoderPlugin::~OpusEncoderPlugin() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
}

std::string OpusEncoderPlugin::GetPluginId() { return kOpusEncoderPluginId; }
std::string OpusEncoderPlugin::GetPluginName() { return "OPUS Encoder"; }
std::string OpusEncoderPlugin::GetVersionName() { return "1.2.0"; }
uint32_t OpusEncoderPlugin::GetVersionCode() { return 120; }
std::string OpusEncoderPlugin::GetPluginDescription() {
    return "OPUS audio encoder";
}

void OpusEncoderPlugin::On1Second() {
#if MEMORY_STST_ON
    const auto info = MemoryStat::Instance()->GetStatInfo();
    LOGI("Memory usage: {}", info.Dump());
#endif
}

bool OpusEncoderPlugin::OnCreate(const PxPluginParam& param) {
    if (!PxAudioEncoderPlugin::OnCreate(param)) {
        return false;
    }
    OpusEncoderRuntime::Config config;
    if (HasParam("save_debug_file")) {
        config.debug_decoder = GetConfigParam<bool>("save_debug_file");
    }
    runtime_ = OpusEncoderRuntime::Make(config);
    RefreshDelivery();
    return true;
}

bool OpusEncoderPlugin::OnStop() {
    if (runtime_) {
        runtime_->ClearDelivery();
        runtime_->Shutdown();
    }
    return PxAudioEncoderPlugin::OnStop();
}

bool OpusEncoderPlugin::OnDestroy() {
    if (runtime_) {
        runtime_->Shutdown();
        runtime_.reset();
    }
    return PxAudioEncoderPlugin::OnDestroy();
}

void OpusEncoderPlugin::Encode(
    const std::shared_ptr<Data>& data,
    int sample_rate,
    int channels,
    int bits) {
    if (!runtime_ || !IsPluginEnabled()) {
        return;
    }
    RefreshDelivery();
    runtime_->Enqueue(data, sample_rate, channels, bits);
}

void OpusEncoderPlugin::RefreshDelivery() {
    if (!runtime_) {
        return;
    }
    const auto weak_runtime = std::weak_ptr<OpusEncoderRuntime>(runtime_);
    const auto weak_context = std::weak_ptr<PxPluginContext>(plugin_context_);
    const auto callback = event_cbk_;
    if (!callback) {
        runtime_->ClearDelivery();
        return;
    }
    runtime_->SetDelivery(
        [weak_runtime, weak_context, callback](
            const std::shared_ptr<Data>& data,
            int sample_rate,
            int channels,
            int bits,
            int frame_size) {
            const auto runtime = weak_runtime.lock();
            const auto context = weak_context.lock();
            if (!runtime || !runtime->IsAccepting() || !context) {
                return;
            }
            auto event = std::make_shared<PxPluginEncodedAudioFrameEvent>();
            event->sample_rate_ = sample_rate;
            event->channels_ = channels;
            event->bits_ = bits;
            event->frame_size_ = frame_size;
            event->data_ = data;
            context->PostWorkTask(
                [weak_runtime, weak_context, callback, event] {
                    const auto queued_runtime = weak_runtime.lock();
                    if (queued_runtime && queued_runtime->IsAccepting() &&
                        weak_context.lock()) {
                        callback(event);
                    }
                });
        });
}

}  // namespace px
