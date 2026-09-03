#include "processors/opus_encoder_processor.h"

#include <chrono>
#include <utility>

#include "opus_encoder_runtime.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"

namespace px::render {
namespace {

RenderError MakeOpusError(std::string operation, std::string reason) {
    return RenderError{
        .code = RenderErrorCode::kModuleLifecycleRejected,
        .component = "opus_encoder",
        .operation = std::move(operation),
        .stage = "processor",
        .reason = std::move(reason),
        .recoverable = true,
    };
}

}  // namespace

std::shared_ptr<OpusEncoderProcessor> OpusEncoderProcessor::Create(
    const std::shared_ptr<EncodedMediaBus>& media_bus,
    EncodedDelivery network_delivery) {
    if (!media_bus) {
        return {};
    }
    return std::make_shared<OpusEncoderProcessor>(
        media_bus, std::move(network_delivery));
}

OpusEncoderProcessor::OpusEncoderProcessor(
    std::shared_ptr<EncodedMediaBus> media_bus,
    EncodedDelivery network_delivery)
    : media_bus_(std::move(media_bus)),
      network_delivery_(std::move(network_delivery)) {}

OpusEncoderProcessor::~OpusEncoderProcessor() {
    static_cast<void>(Stop());
}

BuiltinModuleRegistration OpusEncoderProcessor::MakeRegistration() {
    const std::weak_ptr<OpusEncoderProcessor> weak_owner = weak_from_this();
    return BuiltinModuleRegistration{
        .descriptor = BuiltinModuleDescriptor{
            .id = std::string(kOpusEncoderModuleId),
            .name = "Opus Encoder",
            .author = "GammaRay",
            .description = "Built-in PCM to Opus processor",
            .version_name = "2.0.0",
            .version_code = 200,
            .capability = BuiltinModuleCapability::kProcessor,
            .default_enabled = true,
        },
        .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner
                ? owner->Start()
                : ModuleLifecycleResult(std::unexpected(MakeOpusError(
                      "start", "processor owner expired")));
        },
        .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
            const auto owner = weak_owner.lock();
            co_return owner ? owner->Stop() : ModuleLifecycleResult{};
        },
        .set_enabled = [weak_owner](const bool enabled) {
            const auto owner = weak_owner.lock();
            return owner
                ? owner->SetEnabled(enabled)
                : ModuleLifecycleResult(std::unexpected(MakeOpusError(
                      "set_enabled", "processor owner expired")));
        },
    };
}

ModuleLifecycleResult OpusEncoderProcessor::Start() {
    {
        std::lock_guard lock(mutex_);
        if (running_) {
            return {};
        }
        runtime_ = OpusEncoderRuntime::Make({});
        if (!runtime_) {
            return std::unexpected(MakeOpusError(
                "start", "failed to create encoder runtime"));
        }
        const std::weak_ptr<OpusEncoderProcessor> weak_owner = weak_from_this();
        runtime_->SetDelivery(
            [weak_owner](const std::shared_ptr<Data>& data,
                         const int sample_rate,
                         const int channels,
                         const int bits,
                         const int frame_size) {
                const auto owner = weak_owner.lock();
                if (!owner || !data) {
                    return;
                }
                const auto frame = std::make_shared<const EncodedAudioFrame>(
                    EncodedAudioFrame{
                        .timestamp_us = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now().time_since_epoch())
                                .count()),
                        .codec = "opus",
                        .samples = static_cast<std::uint32_t>(sample_rate),
                        .channels = static_cast<std::uint16_t>(channels),
                        .bits_per_sample = static_cast<std::uint16_t>(bits),
                        .frame_size = static_cast<std::uint32_t>(frame_size),
                        .payload = MakeImmutableByteBuffer(data->AsString()),
                    });
                {
                    std::lock_guard lock(owner->mutex_);
                    if (!owner->running_ || !owner->enabled_) {
                        return;
                    }
                    ++owner->output_packets_;
                }
                owner->media_bus_->PublishEncodedAudio(frame);
                if (owner->network_delivery_) {
                    owner->network_delivery_(frame);
                }
            });
        running_ = true;
    }
    Subscribe();
    LOGI("event=processor.start component=opus_encoder outcome=success");
    return {};
}

ModuleLifecycleResult OpusEncoderProcessor::Stop() {
    Unsubscribe();
    std::shared_ptr<OpusEncoderRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        if (!running_ && !runtime_) {
            return {};
        }
        running_ = false;
        runtime = std::move(runtime_);
    }
    if (runtime) {
        runtime->ClearDelivery();
        runtime->Shutdown();
    }
    LOGI("event=processor.stop component=opus_encoder outcome=success");
    return {};
}

ModuleLifecycleResult OpusEncoderProcessor::SetEnabled(const bool enabled) {
    {
        std::lock_guard lock(mutex_);
        enabled_ = enabled;
    }
    if (enabled) {
        Subscribe();
    }
    else {
        Unsubscribe();
    }
    return {};
}

OpusEncoderSnapshot OpusEncoderProcessor::Snapshot() const {
    std::lock_guard lock(mutex_);
    return OpusEncoderSnapshot{
        .running = running_,
        .enabled = enabled_,
        .input_packets = input_packets_,
        .output_packets = output_packets_,
        .dropped_packets = runtime_ ? runtime_->DroppedCount() : 0,
    };
}

void OpusEncoderProcessor::Subscribe() {
    std::lock_guard lock(mutex_);
    if (!running_ || !enabled_ || input_subscription_) {
        return;
    }
    const std::weak_ptr<OpusEncoderProcessor> weak_owner = weak_from_this();
    input_callback_ =
        std::make_shared<EncodedMediaBus::CapturedAudioCallback>(
            [weak_owner](
                const std::shared_ptr<const CapturedAudioFrame>& frame) {
                if (const auto owner = weak_owner.lock()) {
                    owner->OnCapturedAudio(frame);
                }
            });
    input_subscription_ = media_bus_->SubscribeCapturedAudio(input_callback_);
}

void OpusEncoderProcessor::Unsubscribe() {
    std::shared_ptr<ScopedSubscription> subscription;
    {
        std::lock_guard lock(mutex_);
        subscription = std::move(input_subscription_);
        input_callback_.reset();
    }
    if (subscription) {
        subscription->Reset();
    }
}

void OpusEncoderProcessor::OnCapturedAudio(
    const std::shared_ptr<const CapturedAudioFrame>& frame) {
    if (!frame || !frame->payload || frame->payload->empty()) {
        return;
    }
    std::shared_ptr<OpusEncoderRuntime> runtime;
    {
        std::lock_guard lock(mutex_);
        if (!running_ || !enabled_ || !runtime_) {
            return;
        }
        ++input_packets_;
        runtime = runtime_;
    }
    runtime->Enqueue(
        Data::From(ImmutableByteBufferAsString(frame->payload)),
        static_cast<int>(frame->sample_rate_hz),
        static_cast<int>(frame->channels),
        static_cast<int>(frame->bits_per_sample));
}

}  // namespace px::render
