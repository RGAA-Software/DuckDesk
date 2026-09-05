#include "rtc_client.h"

#include "rtc_connection.h"

#include <utility>

namespace px {

class RtcClient::Impl final {
  public:
    Impl() : connection_(std::make_unique<RtcConnection>()) {}

    std::unique_ptr<RtcConnection> connection_;
};

RtcClient::RtcClient() : impl_(std::make_unique<Impl>()) {}

RtcClient::~RtcClient() {
    if (impl_ && impl_->connection_) {
        static_cast<void>(impl_->connection_->Exit());
    }
}

bool RtcClient::Init(const std::string& remote_device_id) {
    return impl_->connection_->Init(remote_device_id);
}

bool RtcClient::Exit() {
    return impl_->connection_->Exit();
}

bool RtcClient::OnRemoteSdp(const std::string& sdp) {
    return impl_->connection_->OnRemoteSdp(sdp);
}

bool RtcClient::OnRemoteIce(const std::string& ice, const std::string& mid, std::int32_t sdp_mline_index) {
    return impl_->connection_->OnRemoteIce(ice, mid, sdp_mline_index);
}

void RtcClient::SetOnLocalSdpSetCallback(OnLocalSdpSetCallback callback) {
    impl_->connection_->SetOnLocalSdpSetCallback(std::move(callback));
}

void RtcClient::SetOnLocalIceCallback(OnLocalIceCallback callback) {
    impl_->connection_->SetOnLocalIceCallback(std::move(callback));
}

void RtcClient::SetMediaMessageCallback(OnMediaMessageCallback callback) {
    impl_->connection_->SetMediaMessageCallback(callback);
}

void RtcClient::SetFtMessageCallback(OnFtMessageCallback callback) {
    impl_->connection_->SetFtMessageCallback(callback);
}

void RtcClient::SetOnVideoFrameCallback(OnVideoFrameCallback callback) {
    impl_->connection_->SetOnVideoFrameCallback(std::move(callback));
}

void RtcClient::SetOnEncodedVideoFrameCallback(OnEncodedVideoFrameCallback callback) {
    impl_->connection_->SetOnEncodedVideoFrameCallback(std::move(callback));
}

void RtcClient::SetOnAudioDataCallback(OnAudioDataCallback callback) {
    impl_->connection_->SetOnAudioDataCallback(std::move(callback));
}

void RtcClient::SetOnIceStateCallback(OnIceStateCallback callback) {
    impl_->connection_->SetOnIceStateCallback(std::move(callback));
}

void RtcClient::SetOnStatsJsonCallback(OnStatsJsonCallback callback) {
    impl_->connection_->SetOnStatsJsonCallback(std::move(callback));
}

void RtcClient::PostMediaMessage(std::shared_ptr<Data> message) {
    impl_->connection_->PostMediaMessage(std::move(message));
}

void RtcClient::PostFtMessage(std::shared_ptr<Data> message) {
    impl_->connection_->PostFtMessage(std::move(message));
}

void RtcClient::PostInputMessage(std::shared_ptr<Data> message) {
    impl_->connection_->PostInputMessage(std::move(message));
}

std::int64_t RtcClient::GetQueuingMediaMsgCount() const {
    return impl_->connection_->GetQueuingMediaMsgCount();
}

std::int64_t RtcClient::GetQueuingFtMsgCount() const {
    return impl_->connection_->GetQueuingFtMsgCount();
}

bool RtcClient::HasEnoughBufferForQueuingMediaMessages() const {
    return impl_->connection_->HasEnoughBufferForQueuingMediaMessages();
}

bool RtcClient::HasEnoughBufferForQueuingFtMessages() const {
    return impl_->connection_->HasEnoughBufferForQueuingFtMessages();
}

bool RtcClient::IsMediaChannelReady() const {
    return impl_->connection_->IsMediaChannelReady();
}

bool RtcClient::IsFtChannelReady() const {
    return impl_->connection_->IsFtChannelReady();
}

bool RtcClient::IsInputChannelReady() const {
    return impl_->connection_->IsInputChannelReady();
}

void RtcClient::On16msTimeout() {
    impl_->connection_->On16msTimeout();
}

void RtcClient::SetLocalRtcMode(bool enabled) {
    impl_->connection_->SetLocalRtcMode(enabled);
}

void RtcClient::SetIceServersJson(const std::string& json) {
    impl_->connection_->SetIceServersJson(json);
}

bool RtcClient::RestartIce(const std::string& json) {
    return impl_->connection_->RestartIce(json);
}

void RtcClient::SetFileTransferOnly(bool enabled) {
    impl_->connection_->SetFileTransferOnly(enabled);
}

void RtcClient::SetVideoTrackCount(int count) {
    impl_->connection_->SetVideoTrackCount(count);
}

std::shared_ptr<RtcClient> CreateRtcClient() {
    return std::make_shared<RtcClient>();
}

} // namespace px
