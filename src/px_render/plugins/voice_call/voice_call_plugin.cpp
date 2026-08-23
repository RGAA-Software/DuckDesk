#include "voice_call_plugin.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <WtsApi32.h>

#include <chrono>
#include <utility>

#include "px_common_new/data.h"
#include "px_common_new/log.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {
namespace {

uint64_t MonotonicMillis() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::wstring ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        nullptr, 0);
    if (length <= 0) {
        return L"unknown";
    }
    std::wstring result(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
        result.data(), length);
    return result;
}

void CloseOwnedConsentPrompt() {
    const HWND prompt = FindWindowW(nullptr, L"GammaRay Voice Call");
    if (!prompt) {
        return;
    }
    DWORD process_id = 0;
    GetWindowThreadProcessId(prompt, &process_id);
    if (process_id == GetCurrentProcessId()) {
        PostMessageW(prompt, WM_CLOSE, 0, 0);
    }
}

int ShowConsentPromptWithTimeout(const std::wstring& prompt) {
    DWORD session_id = 0;
    DWORD response = IDNO;
    constexpr wchar_t kTitle[] = L"GammaRay Voice Call";
    if (ProcessIdToSessionId(GetCurrentProcessId(), &session_id) &&
        WTSSendMessageW(
            WTS_CURRENT_SERVER_HANDLE, session_id,
            const_cast<LPWSTR>(kTitle),
            static_cast<DWORD>(sizeof(kTitle) - sizeof(wchar_t)),
            const_cast<LPWSTR>(prompt.c_str()),
            static_cast<DWORD>(prompt.size() * sizeof(wchar_t)),
            MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST,
            static_cast<DWORD>(VoiceCallState::kRequestTimeoutMs / 1000),
            &response, TRUE)) {
        return static_cast<int>(response);
    }

    // MessageBoxW runs a nested modal loop, so the plugin's On1Second callback
    // cannot be relied on to dismiss an unanswered prompt. Windows provides the
    // timeout variant from user32 on all supported target versions; resolve it
    // dynamically so older SDK headers do not become a build requirement.
    using MessageBoxTimeoutWFn = int (WINAPI*)(
        HWND, LPCWSTR, LPCWSTR, UINT, WORD, DWORD);
    const auto message_box_timeout = reinterpret_cast<MessageBoxTimeoutWFn>(
        GetProcAddress(GetModuleHandleW(L"user32.dll"), "MessageBoxTimeoutW"));
    if (message_box_timeout) {
        return message_box_timeout(
            nullptr, prompt.c_str(), L"GammaRay Voice Call",
            MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND,
            0, static_cast<DWORD>(VoiceCallState::kRequestTimeoutMs));
    }
    return MessageBoxW(
        nullptr, prompt.c_str(), L"GammaRay Voice Call",
        MB_YESNO | MB_ICONINFORMATION | MB_TOPMOST | MB_SETFOREGROUND);
}

}  // namespace

std::string VoiceCallPlugin::GetPluginId() { return kVoiceCallPluginId; }
std::string VoiceCallPlugin::GetPluginName() { return "Voice Call"; }
std::string VoiceCallPlugin::GetVersionName() { return "1.0.0"; }
uint32_t VoiceCallPlugin::GetVersionCode() { return 100; }
std::string VoiceCallPlugin::GetPluginDescription() {
    return "Authenticated one-to-one voice calls using independent Opus media";
}

bool VoiceCallPlugin::OnCreate(const PxPluginParam& param) {
    PxPluginInterface::OnCreate(param);
    if (HasParam("voice_call_enabled")) {
        enabled_ = GetConfigParam<bool>("voice_call_enabled");
    }
    LOGI("[VoiceCall] plugin ready, enabled={}, protocol=1, format=48000Hz mono 20ms Opus",
         enabled_);
    return true;
}

bool VoiceCallPlugin::OnStop() {
    EndCall(state_.CallId(), false, "render_stopping");
    return PxPluginInterface::OnStop();
}

bool VoiceCallPlugin::OnDestroy() {
    EndCall(state_.CallId(), false, "render_destroyed");
    return PxPluginInterface::OnDestroy();
}

void VoiceCallPlugin::On1Second() {
    PxPluginInterface::On1Second();
    std::string expired_call;
    std::string device_id;
    std::string stream_id;
    uint64_t request_id = 0;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() != VoiceCallPhase::kIncomingPending) {
            return;
        }
        expired_call = state_.CallId();
        request_id = state_.RequestId();
        device_id = active_device_id_;
        stream_id = active_stream_id_;
        if (!state_.Expire(MonotonicMillis())) {
            return;
        }
        active_device_id_.clear();
        active_stream_id_.clear();
    }
    if (!expired_call.empty()) {
        CloseOwnedConsentPrompt();
        SendResponse(device_id, stream_id, expired_call, request_id, false, "timeout");
    }
}

void VoiceCallPlugin::OnNewClientConnected(
    const std::string& visitor_device_id, const std::string& stream_id,
    const std::string& conn_type) {
    PxPluginInterface::OnNewClientConnected(visitor_device_id, stream_id, conn_type);
    if (visitor_device_id.empty() || stream_id.empty()) {
        return;
    }
    std::scoped_lock lock(mutex_);
    connected_clients_[stream_id] = visitor_device_id;
}

void VoiceCallPlugin::OnClientDisconnected(
    const std::string& visitor_device_id, const std::string& stream_id) {
    PxPluginInterface::OnClientDisconnected(visitor_device_id, stream_id);
    std::string call_id;
    {
        std::scoped_lock lock(mutex_);
        connected_clients_.erase(stream_id);
        if (stream_id == active_stream_id_) {
            call_id = state_.CallId();
        }
    }
    if (!call_id.empty()) {
        EndCall(call_id, false, "disconnect");
    }
}

void VoiceCallPlugin::OnMessage(std::shared_ptr<Message> msg) {
    PxPluginInterface::OnMessage(msg);
    if (!msg) {
        return;
    }
    if (msg->type() >= kVoiceCallRequest && msg->type() <= kVoiceAudioConfig) {
        LOGI("[VoiceCall] envelope received, type={}, device={}, stream={}",
             static_cast<int>(msg->type()), msg->device_id(), msg->stream_id());
    }
    if (!enabled_) {
        if (msg->type() == kVoiceCallRequest && msg->voice_call_request().connect()) {
            const auto& request = msg->voice_call_request();
            bool authenticated = false;
            {
                std::scoped_lock lock(mutex_);
                authenticated = IsAuthenticatedSessionLocked(
                    msg->device_id(), msg->stream_id());
            }
            if (authenticated) {
                SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                             request.request_id(), false, "disabled_by_policy");
            }
        }
        return;
    }
    if (msg->type() == kVoiceCallRequest) {
        ProcessRequest(msg);
    } else if (msg->type() == kVoiceAudioFrame) {
        ProcessAudioFrame(msg);
    } else if (msg->type() == kVoiceAudioConfig) {
        const auto& config = msg->voice_audio_config();
        bool active_call = false;
        {
            std::scoped_lock lock(mutex_);
            active_call = IsAuthenticatedSessionLocked(
                              msg->device_id(), msg->stream_id()) &&
                msg->stream_id() == active_stream_id_ &&
                state_.IsMediaAllowed(config.call_id());
        }
        if (!active_call) {
            return;
        }
        if (config.sample_rate() != VoiceAudioEndpoint::kSampleRate ||
            config.channels() != VoiceAudioEndpoint::kChannels ||
            config.frame_ms() != VoiceAudioEndpoint::kFrameMs) {
            LOGW("[VoiceCall] incompatible config dropped, stream={}, call={}",
                 msg->stream_id(), config.call_id());
        }
    }
}

void VoiceCallPlugin::ProcessRequest(const std::shared_ptr<Message>& msg) {
    const auto& request = msg->voice_call_request();
    LOGI("[VoiceCall] request received, connect={}, call={}, request={}, stream={}",
         request.connect(), request.call_id(), request.request_id(), msg->stream_id());
    if (!request.connect()) {
        {
            std::scoped_lock lock(mutex_);
            if (!IsAuthenticatedSessionLocked(msg->device_id(), msg->stream_id()) ||
                msg->stream_id() != active_stream_id_ ||
                request.call_id() != state_.CallId()) {
                return;
            }
        }
        EndCall(request.call_id(), false, "remote_hangup");
        return;
    }

    IncomingVoiceCallResult result;
    bool authenticated = false;
    bool has_cached_decision = false;
    bool cached_accepted = false;
    std::string cached_reason;
    {
        std::scoped_lock lock(mutex_);
        authenticated = IsAuthenticatedSessionLocked(
            msg->device_id(), msg->stream_id());
        if (!authenticated) {
            LOGW("[VoiceCall] unauthenticated request dropped, stream={}", msg->stream_id());
            return;
        }
        result = state_.BeginIncoming(
            request.call_id(), request.request_id(), MonotonicMillis());
        if (result == IncomingVoiceCallResult::kPending) {
            active_device_id_ = msg->device_id();
            active_stream_id_ = msg->stream_id();
            last_decision_valid_ = false;
        } else if (result == IncomingVoiceCallResult::kDuplicate && last_decision_valid_) {
            has_cached_decision = true;
            cached_accepted = last_decision_accepted_;
            cached_reason = last_decision_reason_;
        }
    }

    if (result == IncomingVoiceCallResult::kInvalid) {
        SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                     request.request_id(), false, "invalid_request");
    } else if (result == IncomingVoiceCallResult::kBusy) {
        SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                     request.request_id(), false, "busy");
    } else if (result == IncomingVoiceCallResult::kDuplicate && has_cached_decision) {
        SendResponse(msg->device_id(), msg->stream_id(), request.call_id(),
                     request.request_id(), cached_accepted, cached_reason);
    } else if (result == IncomingVoiceCallResult::kPending) {
        LOGI("[VoiceCall] request pending user consent, call={}, stream={}",
             request.call_id(), msg->stream_id());
        PromptIncoming(msg->device_id(), msg->stream_id(), request.call_id(),
                       request.request_id());
    }
}

void VoiceCallPlugin::PromptIncoming(
    std::string device_id, std::string stream_id, std::string call_id,
    uint64_t request_id) {
    PostUITask([this, device_id = std::move(device_id), stream_id = std::move(stream_id),
                call_id = std::move(call_id), request_id]() {
        LOGI("[VoiceCall] displaying consent prompt, call={}, stream={}", call_id, stream_id);
        const auto prompt = L"Remote device " + ToWide(device_id) +
            L" requests a voice call.\n\nIf accepted, the remote user will hear this "
            L"computer's microphone. Headphones are recommended.";
        const int answer = ShowConsentPromptWithTimeout(prompt);

        if (answer != IDYES) {
            constexpr int kDialogTimeout = 32000;
            const std::string reason = answer == kDialogTimeout ? "timeout" : "rejected";
            bool rejected = false;
            {
                std::scoped_lock lock(mutex_);
                rejected = state_.RejectIncoming(call_id, request_id);
                if (rejected) {
                    last_decision_valid_ = true;
                    last_decision_accepted_ = false;
                    last_decision_reason_ = reason;
                    active_device_id_.clear();
                    active_stream_id_.clear();
                }
            }
            if (rejected) {
                SendResponse(device_id, stream_id, call_id, request_id, false, reason);
            }
            return;
        }

        auto endpoint = std::make_shared<VoiceAudioEndpoint>();
        std::string error;
        const bool started = endpoint->Start(
            [this, device_id, stream_id, call_id](
                uint32_t sequence, uint64_t capture_time_ms,
                const std::vector<uint8_t>& opus) {
                SendAudioFrame(device_id, stream_id, call_id, sequence, capture_time_ms, opus);
            }, &error);
        if (!started) {
            bool rejected = false;
            {
                std::scoped_lock lock(mutex_);
                rejected = state_.RejectIncoming(call_id, request_id);
                if (rejected) {
                    last_decision_valid_ = true;
                    last_decision_accepted_ = false;
                    last_decision_reason_ = "no_mic";
                    active_device_id_.clear();
                    active_stream_id_.clear();
                }
            }
            if (rejected) {
                LOGE("[VoiceCall] audio endpoint failed: {}", error);
                SendResponse(device_id, stream_id, call_id, request_id, false, "no_mic");
            }
            return;
        }

        bool accepted = false;
        {
            std::scoped_lock lock(mutex_);
            accepted = state_.AcceptIncoming(call_id, request_id);
            if (accepted) {
                endpoint_ = std::move(endpoint);
                last_decision_valid_ = true;
                last_decision_accepted_ = true;
                last_decision_reason_.clear();
            }
        }
        if (!accepted) {
            endpoint->Stop();
            return;
        }
        SendResponse(device_id, stream_id, call_id, request_id, true, "");
        SendConfig(device_id, stream_id, call_id);
        LOGI("[VoiceCall] accepted, stream={}, call={}", stream_id, call_id);
    });
}

void VoiceCallPlugin::ProcessAudioFrame(const std::shared_ptr<Message>& msg) {
    const auto& frame = msg->voice_audio_frame();
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    {
        std::scoped_lock lock(mutex_);
        if (!IsAuthenticatedSessionLocked(msg->device_id(), msg->stream_id()) ||
            msg->stream_id() != active_stream_id_ ||
            !state_.AcceptMedia(frame.call_id(), frame.sequence())) {
            return;
        }
        endpoint = endpoint_;
    }
    if (endpoint && !frame.opus().empty()) {
        endpoint->ReceiveOpus(frame.opus().data(), frame.opus().size());
    }
}

void VoiceCallPlugin::SendResponse(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint64_t request_id, bool accepted,
    const std::string& reason) {
    Message message;
    message.set_type(kVoiceCallResponse);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* response = message.mutable_voice_call_response();
    response->set_call_id(call_id);
    response->set_request_id(request_id);
    response->set_accepted(accepted);
    response->set_reason(reason);
    if (auto data = ProtoAsData(&message); data) {
        DispatchTargetStreamMessage(stream_id, data, true);
    }
}

void VoiceCallPlugin::SendConfig(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id) {
    Message message;
    message.set_type(kVoiceAudioConfig);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* config = message.mutable_voice_audio_config();
    config->set_call_id(call_id);
    config->set_sample_rate(VoiceAudioEndpoint::kSampleRate);
    config->set_channels(VoiceAudioEndpoint::kChannels);
    config->set_frame_ms(VoiceAudioEndpoint::kFrameMs);
    config->set_bitrate_bps(VoiceAudioEndpoint::kBitrateBps);
    config->set_fec(true);
    config->set_dtx(false);
    if (auto data = ProtoAsData(&message); data) {
        DispatchTargetStreamMessage(stream_id, data, true);
    }
}

void VoiceCallPlugin::SendAudioFrame(
    const std::string& device_id, const std::string& stream_id,
    const std::string& call_id, uint32_t sequence, uint64_t capture_time_ms,
    const std::vector<uint8_t>& opus) {
    {
        std::scoped_lock lock(mutex_);
        if (!state_.IsMediaAllowed(call_id) || stream_id != active_stream_id_) {
            return;
        }
    }
    Message message;
    message.set_type(kVoiceAudioFrame);
    message.set_device_id(device_id);
    message.set_stream_id(stream_id);
    auto* frame = message.mutable_voice_audio_frame();
    frame->set_call_id(call_id);
    frame->set_sequence(sequence);
    frame->set_capture_time_ms(capture_time_ms);
    frame->set_opus(opus.data(), opus.size());
    if (auto data = ProtoAsData(&message); data) {
        DispatchTargetStreamMessage(stream_id, data, true);
    }
}

void VoiceCallPlugin::EndCall(
    const std::string& call_id, bool notify_remote, const std::string& reason) {
    std::shared_ptr<VoiceAudioEndpoint> endpoint;
    std::string device_id;
    std::string stream_id;
    uint64_t request_id = 0;
    {
        std::scoped_lock lock(mutex_);
        if (state_.Phase() == VoiceCallPhase::kIdle || state_.CallId() != call_id) {
            return;
        }
        request_id = state_.RequestId();
        state_.HangUp(call_id);
        endpoint = std::move(endpoint_);
        device_id = std::move(active_device_id_);
        stream_id = std::move(active_stream_id_);
        last_decision_valid_ = false;
    }
    if (endpoint) {
        const auto stats = endpoint->Stats();
        endpoint->Stop();
        LOGI("[VoiceCall] ended reason={}, tx={}, rx={}, underrun={}", reason,
             stats.encoded_packets, stats.decoded_packets, stats.playout_underruns);
    }
    CloseOwnedConsentPrompt();
    if (notify_remote && !stream_id.empty()) {
        Message message;
        message.set_type(kVoiceCallRequest);
        message.set_device_id(device_id);
        message.set_stream_id(stream_id);
        auto* request = message.mutable_voice_call_request();
        request->set_call_id(call_id);
        request->set_request_id(request_id);
        request->set_connect(false);
        if (auto data = ProtoAsData(&message); data) {
            DispatchTargetStreamMessage(stream_id, data, true);
        }
    }
}

bool VoiceCallPlugin::IsAuthenticatedSessionLocked(
    const std::string& device_id, const std::string& stream_id) const {
    if (device_id.empty() || stream_id.empty()) {
        return false;
    }
    // The connection callback supplies the authenticated visitor device id,
    // while media envelopes carry the controlled/render device id.  The
    // authenticated stream id is the stable binding shared by both paths.
    return connected_clients_.contains(stream_id);
}

}  // namespace px
