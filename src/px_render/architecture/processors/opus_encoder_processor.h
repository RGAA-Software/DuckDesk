#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include "modules/builtin_module_catalog.h"
#include "pipeline/encoded_media_bus.h"

namespace px {
class OpusEncoderRuntime;
}

namespace px::render {

inline constexpr std::string_view kOpusEncoderModuleId =
    "e25954da-205e-430f-af61-99e0e200d119";

struct OpusEncoderSnapshot final {
    bool running{false};
    bool enabled{true};
    std::uint64_t input_packets{0};
    std::uint64_t output_packets{0};
    std::uint64_t dropped_packets{0};
};

class OpusEncoderProcessor final
    : public std::enable_shared_from_this<OpusEncoderProcessor> {
public:
    using EncodedDelivery = std::function<void(
        const std::shared_ptr<const EncodedAudioFrame>&)>;

    [[nodiscard]] static std::shared_ptr<OpusEncoderProcessor> Create(
        const std::shared_ptr<EncodedMediaBus>& media_bus,
        EncodedDelivery network_delivery = {});
    OpusEncoderProcessor(
        std::shared_ptr<EncodedMediaBus> media_bus,
        EncodedDelivery network_delivery);
    ~OpusEncoderProcessor();

    OpusEncoderProcessor(const OpusEncoderProcessor&) = delete;
    OpusEncoderProcessor& operator=(const OpusEncoderProcessor&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);
    [[nodiscard]] OpusEncoderSnapshot Snapshot() const;

private:
    void Subscribe();
    void Unsubscribe();
    void OnCapturedAudio(
        const std::shared_ptr<const CapturedAudioFrame>& frame);

    const std::shared_ptr<EncodedMediaBus> media_bus_;
    const EncodedDelivery network_delivery_;
    mutable std::mutex mutex_;
    std::shared_ptr<OpusEncoderRuntime> runtime_;
    std::shared_ptr<EncodedMediaBus::CapturedAudioCallback> input_callback_;
    std::shared_ptr<ScopedSubscription> input_subscription_;
    bool running_{false};
    bool enabled_{true};
    std::uint64_t input_packets_{0};
    std::uint64_t output_packets_{0};
};

}  // namespace px::render
