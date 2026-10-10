#include "frontend_admission.h"

#include <algorithm>

#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/privacy_log.h"
#include "px_common/uuid.h"
#include "px_render/architecture/events/render_event.h"
#include "px_render/network/direct_stream_id.h"
#include "px_render/network/ws/ws_callback_workflow.h"
#include "px_render/network/ws/ws_transport.h"

namespace px {
static int64_t CurrentSystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void DispatchCloseLogicalSessionBinding(const std::weak_ptr<WsTransport>& transport, const std::string& logical_session_id,
                                        const std::string& binding_id) {
    const auto owner = transport.lock();
    if (!owner || logical_session_id.empty() || binding_id.empty()) {
        return;
    }
    const auto event = std::make_shared<CloseLogicalSessionBindingEvent>();
    event->logical_session_id_ = logical_session_id;
    event->binding_id_ = binding_id;
    owner->EmitEvent(event);
}

static PxAwaitable<PxResult<FrontendAdmission>> AuthenticateFrontendPasswordAsync(std::weak_ptr<WsTransport> transport,
                                                                                  std::unordered_map<std::string, std::string> query_parameters,
                                                                                  std::string remote_address, const bool media_stream) {
    const auto owner = transport.lock();
    std::string stream_id{};
    std::string client_nonce{};
    std::string password_digest{};
    {
        const auto stream_iterator = query_parameters.find("stream_id");
        const auto nonce_iterator = query_parameters.find("client_nonce");
        const auto password_iterator = query_parameters.find("safety_pwd_md5");
        if (!owner || stream_iterator == query_parameters.end() || stream_iterator->second.empty() || nonce_iterator == query_parameters.end() ||
            nonce_iterator->second.empty() || password_iterator == query_parameters.end() || password_iterator->second.empty()) {
            co_return PxResult<FrontendAdmission>::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "ws_password_auth", "device password is missing"));
        }
        stream_id = stream_iterator->second;
        client_nonce = nonce_iterator->second;
        password_digest = password_iterator->second;
    }
    const auto settings = owner->Settings();
    const bool valid_safety_password = !settings.device_safety_password.empty() && settings.device_safety_password == password_digest;
    const bool valid_temporary_password = !settings.device_random_password.empty() && MD5::Hex(settings.device_random_password) == password_digest;
    if ((!settings.device_safety_password.empty() || !settings.device_random_password.empty()) && !valid_safety_password &&
        !valid_temporary_password) {
        co_return PxResult<FrontendAdmission>::Failure(MakePxAsyncError(PxAsyncErrorCode::kServiceRejected, "ws_password_auth",
                                                                        "device password was rejected", false, "SESSION_PASSWORD_REJECTED"));
    }
    // RDP runtime authorization sends this identifier through Console's strict
    // binding validator, whose portable identifier alphabet is [A-Za-z0-9_-].
    const std::string logical_session_id{"password-" + MD5::Hex(stream_id + "|" + client_nonce + "|" + remote_address)};
    std::string quota_id{};
    std::uint32_t valid_for_ms{};
    if (media_stream) {
        quota_id = DirectStreamQuotaId(settings.device_id, logical_session_id);
        if (quota_id.empty()) {
            co_return PxResult<FrontendAdmission>::Failure(
                MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "direct_stream_admission", "direct stream identity is invalid"));
        }
        auto direct_grant = co_await owner->RequestDirectStream(quota_id, false, std::chrono::steady_clock::now() + std::chrono::seconds(12));
        if (!direct_grant.HasValue() || direct_grant.Value() == 0) {
            co_return PxResult<FrontendAdmission>::Failure(
                direct_grant.HasValue()
                    ? MakePxAsyncError(PxAsyncErrorCode::kServiceRejected, "direct_stream_admission", "Console denied direct stream")
                    : direct_grant.Error());
        }
        valid_for_ms = direct_grant.TakeValue();
    }
    co_return PxResult<FrontendAdmission>::Success(FrontendAdmission{
        .permissions_ = {"view", "input", "clipboard", "file", "audio", "rdp"},
        .logical_session_id_ = logical_session_id,
        .stream_id_ = stream_id,
        .join_mode_ = "control",
        .subject_id_ = "password:" + MD5::Hex(remote_address + "|" + client_nonce),
        .expires_at_ms_ = valid_for_ms == 0 ? 0 : CurrentSystemMilliseconds() + static_cast<std::int64_t>(valid_for_ms),
        .allow_observer_ = false,
        .allow_takeover_ = true,
        .direct_quota_id_ = quota_id,
        .direct_valid_for_ms_ = valid_for_ms,
    });
}

PxAwaitable<PxResult<FrontendAdmission>> AuthenticateFrontendAsync(std::weak_ptr<WsTransport> transport,
                                                                   std::unordered_map<std::string, std::string> query_parameters,
                                                                   std::string remote_address, const bool media_stream) {
    const auto owner = transport.lock();
    if (!owner) {
        co_return PxResult<FrontendAdmission>::Failure(
            MakePxAsyncError(PxAsyncErrorCode::kServiceStopped, "ws_frontend_auth", "transport is unavailable", true, "TRANSPORT_UNAVAILABLE"));
    }
    const bool console_descriptor_present = query_parameters.contains("frontend_token") || query_parameters.contains("session_id") ||
                                            query_parameters.contains("session_revision");
    if (!owner->RequiresConsoleFrontendAdmission() && !console_descriptor_present) {
        co_return co_await AuthenticateFrontendPasswordAsync(std::move(transport), std::move(query_parameters), std::move(remote_address),
                                                             media_stream);
    }

    auto descriptor = ConsumeWebSocketFrontendDescriptor(query_parameters);
    if (!descriptor) {
        co_return PxResult<FrontendAdmission>::Failure(MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "ws_frontend_auth",
                                                                        "Console frontend descriptor is invalid", false,
                                                                        "CONSOLE_FRONTEND_DESCRIPTOR_INVALID"));
    }

    auto admitted = co_await owner->AdmitFrontend(
        ConsoleFrontendAdmissionRequest{
            .request_id = GenerateRandomBase64Id(),
            .session_id = descriptor->session_id,
            .revision = descriptor->revision,
            .frontend_token = descriptor->token->Copy(),
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(12));
    if (!admitted.HasValue()) {
        co_return PxResult<FrontendAdmission>::Failure(admitted.Error());
    }
    auto grant = admitted.TakeValue();
    const auto settings = owner->Settings();
    if (!IsAcceptedWebSocketFrontendGrant(*descriptor, settings.application_instance_id, settings.device_id, grant)) {
        LOGW(
            "event=session.frontend_identity_mismatch component=net_ws code=CONSOLE_FRONTEND_IDENTITY_MISMATCH "
            "operation=admit_frontend outcome=rejected recoverable=false target_kind={} instance_match={} device_match={} role_match={} "
            "session_match={} revision_match={} lease_valid={}",
            grant.target_kind, grant.instance_id == settings.application_instance_id, grant.device_id == settings.device_id,
            grant.access_role == "controller" || grant.access_role == "observer", grant.session_id == descriptor->session_id,
            grant.revision == descriptor->revision, grant.valid_for_ms > 0);
        co_return PxResult<FrontendAdmission>::Failure(MakePxAsyncError(PxAsyncErrorCode::kServiceRejected, "ws_frontend_auth",
                                                                        "Console frontend identity was rejected", false,
                                                                        "CONSOLE_FRONTEND_IDENTITY_MISMATCH"));
    }
    const bool controller = grant.access_role == "controller";
    co_return PxResult<FrontendAdmission>::Success(FrontendAdmission{
        .permissions_ =
            controller ? std::vector<std::string>{"view", "input", "clipboard", "file", "audio", "rdp"} : std::vector<std::string>{"view", "audio"},
        .logical_session_id_ = grant.session_id,
        .stream_id_ = descriptor->stream_id,
        .join_mode_ = controller ? "control" : "observe",
        .subject_id_ = grant.client_type + ":" + grant.session_id,
        .expires_at_ms_ = CurrentSystemMilliseconds() + static_cast<std::int64_t>(grant.valid_for_ms),
        .allow_observer_ = !controller,
        .allow_takeover_ = false,
        .frontend_token_ = descriptor->token,
        .console_frontend_grant_ = std::move(grant),
        .descriptor_session_id_ = descriptor->session_id,
        .descriptor_revision_ = descriptor->revision,
    });
}

PxAwaitable<PxResult<LogicalSessionAdmission>> AdmitFrontendSessionAsync(std::weak_ptr<WsTransport> weak_transport, LogicalSessionGrant grant,
                                                                         const LogicalSessionTransport session_transport, std::string binding_id) {
    const auto logical_session_id = grant.logical_session_id;
    co_return co_await AwaitWsValueCallback<LogicalSessionAdmission>(
        [weak_transport, grant = std::move(grant), session_transport, binding_id](std::function<void(LogicalSessionAdmission)> completion) {
            const auto owner = weak_transport.lock();
            if (!owner) {
                return false;
            }
            const auto event = std::make_shared<AdmitLogicalSessionEvent>();
            event->grant_ = grant;
            event->transport_ = session_transport;
            event->binding_id_ = binding_id;
            event->callback_ = std::move(completion);
            owner->EmitEvent(event);
            return true;
        },
        std::chrono::steady_clock::now() + std::chrono::seconds(3), "ws_session_admit",
        [weak_transport, logical_session_id, binding_id](const LogicalSessionAdmission& admission) {
            if (admission.code == LogicalSessionAdmissionCode::kAccepted) {
                DispatchCloseLogicalSessionBinding(weak_transport, logical_session_id, binding_id);
            }
        });
}

}  // namespace px
