#include "client_session.h"

#include "client_audio_output.h"
#include "ct_virtual_display_protocol.h"
#include "px_client_sdk/platform/windows/windows_decoder_factory.h"
#include "px_client_sdk/platform/windows/windows_video_resources.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_connection_params.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_recording_session.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_voice_call.h"
#include "px_client_sdk/platform/voice_audio_endpoint_port.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/data.h"
#include "px_common/md5.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"
#include "px_common/url_helper.h"
#include "px_message/proto_converter.h"
#include "px_message/proto_message_maker.h"
#include "px_message.pb.h"
#include "px_ft_engine/ft_async_session.h"
#include "px_ft_engine/ft_engine.h"
#include "px_rdp/rdp_client_endpoint.h"
#include "px_rdp/rdp_stream_packet.h"
#include "rdp/rdp_session.h"

#include <SDL3/SDL.h>
#include <freerdp/input.h>
#include <Windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <format>
#include <utility>

namespace px::client::imgui {

bool ClientSession::InitializeRdp() {
    if (!px::rdp::InitializeRdpRuntime())
        return false;
    notifier_ = std::make_shared<px::MessageNotifier>();
    listener_ = notifier_->CreateListener(px::MessageExecutionLane::kControl);
    if (!listener_)
        return false;
    px::SdkConnectionParams params{};
    params.session_mode_ = px::SdkSessionMode::kRdp;
    params.media_transport_ = px::SdkMediaTransport::kWebSocket;
    params.ip_ = config_.host;
    params.port_ = config_.port;
    params.stream_id_ = config_.streamId;
    params.device_id_ = config_.localDeviceId;
    params.connection_nonce_ = config_.nonce;
    params.connection_instance_id_ = config_.instanceId;
    params.media_path_ =
        std::format("/media?stream_id={}&remote_device_id={}&visitor_device_id={}&safety_pwd_md5={}",
                    px::UrlHelper::EncodeQueryComponent(config_.streamId), px::UrlHelper::EncodeQueryComponent(config_.remoteDeviceId),
                    px::UrlHelper::EncodeQueryComponent(config_.localDeviceId), px::UrlHelper::EncodeQueryComponent(config_.remotePasswordHash));
    rdpNetwork_ = std::make_shared<px::NetClient>(std::move(params), notifier_);
    const std::weak_ptr<ClientSession> weakSelf{shared_from_this()};
    rdpNetwork_->SetOnConnectCallback([weakSelf] {
        if (const auto self = weakSelf.lock())
            self->SetState(ClientConnectionState::Connecting, "RDP transport connected");
    });
    rdpNetwork_->SetOnDisconnectedCallback([weakSelf] {
        if (const auto self = weakSelf.lock(); self && !self->stopped_.load()) {
            self->SetState(ClientConnectionState::Disconnected, "RDP transport disconnected");
        }
    });
    listener_->Listen<px::SdkMsgWsConnectionRejected>([weakSelf](const px::SdkMsgWsConnectionRejected& event) {
        const auto self = weakSelf.lock();
        if (!self)
            return;
        switch (event.rejection_) {
        case px::WsControlRejection::kAuthorization:
            self->SetState(ClientConnectionState::Rejected, "The device password was rejected", ClientConnectionFailure::Authorization);
            break;
        case px::WsControlRejection::kRemoteAccessDisabled:
            self->SetState(ClientConnectionState::Rejected, "Remote access is disabled on the remote device",
                           ClientConnectionFailure::RemoteAccessDisabled);
            break;
        case px::WsControlRejection::kOccupied:
            self->SetState(ClientConnectionState::Rejected, "The device is in use or within the reconnect grace period",
                           ClientConnectionFailure::Occupied);
            break;
        case px::WsControlRejection::kSessionPolicy:
            self->SetState(ClientConnectionState::Rejected, "The device policy does not allow this connection",
                           ClientConnectionFailure::SessionPolicy);
            break;
        case px::WsControlRejection::kNone:
        default:
            self->SetState(ClientConnectionState::Rejected, "The RDP control channel rejected the connection", ClientConnectionFailure::Transport);
            break;
        }
    });
    const auto opened = std::make_shared<std::atomic_bool>(false);
    rdpNetwork_->SetOnRdpMessageCallback([weakSelf, opened](std::shared_ptr<px::Data> wire) {
        const auto self = weakSelf.lock();
        if (!self || !wire || self->stopped_.load())
            return;
        if (!opened->exchange(true)) {
            const auto binding = px::rdp::DecodeOpen(wire->Bytes());
            if (!binding) {
                self->SetState(ClientConnectionState::Rejected, "The RDP channel returned invalid session data", ClientConnectionFailure::Transport);
                return;
            }
            const std::weak_ptr<px::NetClient> weakNetwork{self->rdpNetwork_};
            auto endpoint = px::rdp::RdpClientEndpoint::Create(
                self->notifier_->GetAsyncRuntime()->Executor(px::PxAsyncLane::kWorker), *binding,
                [weakNetwork](std::shared_ptr<px::Data> bytes, px::rdp::RdpTcpBridge::SendCompletion completion) {
                    if (const auto network = weakNetwork.lock())
                        network->PostRdpMessage(std::move(bytes), std::move(completion));
                    else
                        completion(false);
                },
                [weakSelf](const std::uint16_t port) {
                    if (const auto owner = weakSelf.lock())
                        owner->StartRdpProtocol(port);
                },
                [weakSelf](px::rdp::BridgeCloseReason) {
                    if (const auto owner = weakSelf.lock(); owner && !owner->stopped_.load()) {
                        owner->SetState(ClientConnectionState::Disconnected, "RDP channel closed");
                    }
                });
            if (!endpoint) {
                self->SetState(ClientConnectionState::Rejected, "The local RDP bridge could not be started", ClientConnectionFailure::Transport);
                return;
            }
            const std::scoped_lock lock{self->mutex_};
            self->rdpEndpoint_ = std::move(endpoint);
            return;
        }
        std::shared_ptr<px::rdp::RdpClientEndpoint> endpoint{};
        {
            const std::scoped_lock lock{self->mutex_};
            endpoint = self->rdpEndpoint_;
        }
        if (endpoint)
            static_cast<void>(endpoint->Receive(std::move(wire)));
    });
    return true;
}

void ClientSession::StartRdpProtocol(const std::uint16_t loopbackPort) {
    px::rdp::SessionConfiguration configuration{.loopbackPort = loopbackPort,
                                                .account = config_.rdpAccount,
                                                .domain = config_.rdpDomain,
                                                .proxyCertificateSha256 = config_.rdpProxyCertificateSha256,
                                                .password = std::make_shared<px::rdp::SessionSecret>(config_.rdpPassword->Bytes()),
                                                .desktop = {1440, 900},
                                                .audio = config_.audio,
                                                .clipboard = config_.clipboard};
    const std::weak_ptr<ClientSession> weakSelf{shared_from_this()};
    auto session = px::rdp::RdpSession::Create(std::move(configuration),
                                               {.frame =
                                                    [weakSelf](std::shared_ptr<const px::rdp::DesktopFrame> frame) {
                                                        if (const auto self = weakSelf.lock())
                                                            self->ApplyRdpFrame(frame);
                                                    },
                                                .phase =
                                                    [weakSelf](const px::rdp::SessionPhase phase, std::string reason) {
                                                        if (const auto self = weakSelf.lock()) {
                                                            switch (phase) {
                                                            case px::rdp::SessionPhase::Connecting:
                                                                self->SetState(ClientConnectionState::Connecting, "Connecting RDP workspace");
                                                                break;
                                                            case px::rdp::SessionPhase::Connected:
                                                                self->SetState(ClientConnectionState::Connected, "RDP connected");
                                                                break;
                                                            case px::rdp::SessionPhase::Disconnected:
                                                                if (!self->stopped_.load())
                                                                    self->SetState(ClientConnectionState::Disconnected, "RDP workspace disconnected");
                                                                break;
                                                            case px::rdp::SessionPhase::Failed:
                                                                self->SetState(ClientConnectionState::Rejected, std::move(reason));
                                                                break;
                                                            }
                                                        }
                                                    },
                                                .clipboard =
                                                    [weakSelf](std::string text) {
                                                        if (const auto self = weakSelf.lock()) {
                                                            const std::scoped_lock lock{self->mutex_};
                                                            self->remoteClipboardText_ = std::move(text);
                                                        }
                                                    }});
    if (!session) {
        SetState(ClientConnectionState::Rejected, "RDP protocol initialization failed");
        return;
    }
    const std::scoped_lock lock{mutex_};
    rdpSession_ = std::move(session);
}

void ClientSession::ApplyRdpFrame(const std::shared_ptr<const px::rdp::DesktopFrame>& frame) {
    if (!frame || !frame->IsValid())
        return;
    std::shared_ptr<px::rdp::RdpSession> session{};
    {
        const std::scoped_lock lock{mutex_};
        if (rdpWidth_ != frame->desktop.width || rdpHeight_ != frame->desktop.height) {
            rdpWidth_ = frame->desktop.width;
            rdpHeight_ = frame->desktop.height;
            rdpFrameBuffer_.assign(static_cast<std::size_t>(rdpWidth_) * rdpHeight_ * 4U, 0U);
        }
        std::size_t sourceOffset{};
        for (const auto& rectangle : frame->rectangles) {
            const auto rowBytes = static_cast<std::size_t>(rectangle.width) * 4U;
            for (int row{}; row < rectangle.height; ++row) {
                const auto destination = (static_cast<std::size_t>(rectangle.y + row) * rdpWidth_ + rectangle.x) * 4U;
                std::ranges::copy_n(frame->pixels.begin() + static_cast<std::ptrdiff_t>(sourceOffset), rowBytes,
                                    rdpFrameBuffer_.begin() + static_cast<std::ptrdiff_t>(destination));
                sourceOffset += rowBytes;
            }
        }
        latestFrame_ = std::make_shared<ClientVideoFrame>(ClientVideoFrame{.width = rdpWidth_, .height = rdpHeight_, .bgra = rdpFrameBuffer_});
        session = rdpSession_;
    }
    if (session)
        session->ConsumeFrame(frame->frameId);
}
} // namespace px::client::imgui
