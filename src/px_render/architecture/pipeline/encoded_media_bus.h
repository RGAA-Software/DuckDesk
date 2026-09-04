#pragma once

#include <memory>
#include <functional>

#include "media_types.h"
#include "runtime/scoped_subscription.h"

namespace px::render {

// Synchronous typed fan-out for the one-way media pipeline. Subscribers only
// validate/enqueue; blocking work belongs to their owned workers. Separate
// registries make producer-side copy decisions explicit for each media kind.
class EncodedMediaBus final {
public:
    using VideoCallback =
        std::function<void(const std::shared_ptr<const EncodedVideoFrame>&)>;
    using CapturedVideoCallback =
        std::function<void(const std::shared_ptr<const CapturedVideoFrame>&)>;
    using EncodedAudioCallback =
        std::function<void(const std::shared_ptr<const EncodedAudioFrame>&)>;
    using CapturedAudioCallback =
        std::function<void(const std::shared_ptr<const CapturedAudioFrame>&)>;
    using ClientConnectedCallback =
        std::function<void(const MediaClientConnected&)>;
    using ClientDisconnectedCallback =
        std::function<void(const MediaClientDisconnected&)>;

    [[nodiscard]] static std::shared_ptr<EncodedMediaBus> Create();

    [[nodiscard]] std::shared_ptr<ScopedSubscription> SubscribeVideo(
        const std::shared_ptr<VideoCallback>& callback);
    [[nodiscard]] std::shared_ptr<ScopedSubscription> SubscribeCapturedVideo(
        const std::shared_ptr<CapturedVideoCallback>& callback);
    [[nodiscard]] std::shared_ptr<ScopedSubscription> SubscribeEncodedAudio(
        const std::shared_ptr<EncodedAudioCallback>& callback);
    [[nodiscard]] std::shared_ptr<ScopedSubscription> SubscribeCapturedAudio(
        const std::shared_ptr<CapturedAudioCallback>& callback);
    [[nodiscard]] std::shared_ptr<ScopedSubscription> SubscribeClientConnected(
        const std::shared_ptr<ClientConnectedCallback>& callback);
    [[nodiscard]] std::shared_ptr<ScopedSubscription> SubscribeClientDisconnected(
        const std::shared_ptr<ClientDisconnectedCallback>& callback);

    void PublishVideo(
        const std::shared_ptr<const EncodedVideoFrame>& frame) const;
    void PublishCapturedVideo(
        const std::shared_ptr<const CapturedVideoFrame>& frame) const;
    void PublishEncodedAudio(
        const std::shared_ptr<const EncodedAudioFrame>& frame) const;
    void PublishCapturedAudio(
        const std::shared_ptr<const CapturedAudioFrame>& frame) const;
    void PublishClientConnected(const MediaClientConnected& event) const;
    void PublishClientDisconnected(const MediaClientDisconnected& event) const;

    [[nodiscard]] bool NeedsVideo() const;
    [[nodiscard]] bool NeedsCapturedVideo() const;
    [[nodiscard]] bool NeedsEncodedAudio() const;
    [[nodiscard]] bool NeedsCapturedAudio() const;

private:
    std::shared_ptr<SubscriptionRegistry<
        std::shared_ptr<const EncodedVideoFrame>>> video_registry_ =
        SubscriptionRegistry<std::shared_ptr<const EncodedVideoFrame>>::Create();
    std::shared_ptr<SubscriptionRegistry<
        std::shared_ptr<const CapturedVideoFrame>>> captured_video_registry_ =
        SubscriptionRegistry<std::shared_ptr<const CapturedVideoFrame>>::Create();
    std::shared_ptr<SubscriptionRegistry<
        std::shared_ptr<const EncodedAudioFrame>>> encoded_audio_registry_ =
        SubscriptionRegistry<std::shared_ptr<const EncodedAudioFrame>>::Create();
    std::shared_ptr<SubscriptionRegistry<
        std::shared_ptr<const CapturedAudioFrame>>> captured_audio_registry_ =
        SubscriptionRegistry<std::shared_ptr<const CapturedAudioFrame>>::Create();
    std::shared_ptr<SubscriptionRegistry<MediaClientConnected>>
        client_connected_registry_ =
            SubscriptionRegistry<MediaClientConnected>::Create();
    std::shared_ptr<SubscriptionRegistry<MediaClientDisconnected>>
        client_disconnected_registry_ =
            SubscriptionRegistry<MediaClientDisconnected>::Create();
};

}  // namespace px::render
