#include "pipeline/encoded_media_bus.h"

namespace px::render {

std::shared_ptr<EncodedMediaBus> EncodedMediaBus::Create() {
    return std::make_shared<EncodedMediaBus>();
}

std::shared_ptr<ScopedSubscription> EncodedMediaBus::SubscribeVideo(
    const std::shared_ptr<VideoCallback>& callback) {
    return video_registry_->Subscribe(callback);
}

std::shared_ptr<ScopedSubscription> EncodedMediaBus::SubscribeEncodedAudio(
    const std::shared_ptr<EncodedAudioCallback>& callback) {
    return encoded_audio_registry_->Subscribe(callback);
}

std::shared_ptr<ScopedSubscription> EncodedMediaBus::SubscribeCapturedAudio(
    const std::shared_ptr<CapturedAudioCallback>& callback) {
    return captured_audio_registry_->Subscribe(callback);
}

std::shared_ptr<ScopedSubscription> EncodedMediaBus::SubscribeClientConnected(
    const std::shared_ptr<ClientConnectedCallback>& callback) {
    return client_connected_registry_->Subscribe(callback);
}

std::shared_ptr<ScopedSubscription>
EncodedMediaBus::SubscribeClientDisconnected(
    const std::shared_ptr<ClientDisconnectedCallback>& callback) {
    return client_disconnected_registry_->Subscribe(callback);
}

void EncodedMediaBus::PublishVideo(
    const std::shared_ptr<const EncodedVideoFrame>& frame) const {
    video_registry_->Dispatch(frame);
}

void EncodedMediaBus::PublishEncodedAudio(
    const std::shared_ptr<const EncodedAudioFrame>& frame) const {
    encoded_audio_registry_->Dispatch(frame);
}

void EncodedMediaBus::PublishCapturedAudio(
    const std::shared_ptr<const CapturedAudioFrame>& frame) const {
    captured_audio_registry_->Dispatch(frame);
}

void EncodedMediaBus::PublishClientConnected(
    const MediaClientConnected& event) const {
    client_connected_registry_->Dispatch(event);
}

void EncodedMediaBus::PublishClientDisconnected(
    const MediaClientDisconnected& event) const {
    client_disconnected_registry_->Dispatch(event);
}

bool EncodedMediaBus::NeedsVideo() const {
    return video_registry_->Size() > 0;
}

bool EncodedMediaBus::NeedsEncodedAudio() const {
    return encoded_audio_registry_->Size() > 0;
}

bool EncodedMediaBus::NeedsCapturedAudio() const {
    return captured_audio_registry_->Size() > 0;
}

}  // namespace px::render
