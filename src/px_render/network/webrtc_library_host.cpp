#include "network/webrtc_library_host.h"

#include <any>
#include <map>
#include <utility>

#include "px_common_new/log.h"
#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_net_plugin.h"

namespace px {
namespace {

using WebRtcFactory = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary): established WebRTC DLL factory ABI

PxPluginParam MakeCompatibilityConfiguration(
    const std::string& base_name,
    const WebRtcLibraryConfiguration& configuration) {
    return PxPluginParam{
        .cluster_ = {
            {"name", base_name + ".dll"},
            {"base_path", configuration.base_path},
            {"base_data_path", configuration.base_data_path},
            {"capture_audio_device_id", configuration.capture_audio_device_id},
            {"device_id", configuration.device_id},
            {"direct_allow_takeover", configuration.direct_allow_takeover},
            {"relay_enabled", configuration.relay_enabled},
            {"language", static_cast<std::int64_t>(configuration.language)},
            {"appkey", configuration.appkey},
        },
    };
}

PxPluginSettingsInfo MakeCompatibilitySettings(
    const WebRtcLibrarySettings& settings) {
    PxPluginSettingsInfo result;
    result.device_id_ = settings.device_id;
    result.device_random_pwd_ = settings.device_random_password;
    result.device_safety_pwd_ = settings.device_safety_password;
    result.relay_host_ = settings.relay_host;
    result.relay_port_ = settings.relay_port;
    result.can_be_operated_ = settings.can_be_operated;
    result.direct_allow_takeover_ = settings.direct_allow_takeover;
    result.relay_enabled_ = settings.relay_enabled;
    result.language_ = settings.language;
    result.file_transfer_enabled_ = settings.file_transfer_enabled;
    result.audio_enabled_ = settings.audio_enabled;
    result.appkey_ = settings.appkey;
    result.max_transmit_speed_ = settings.max_transmit_speed;
    result.max_receive_speed_ = settings.max_receive_speed;
    result.role_ = settings.role;
    return result;
}

}  // namespace

class WebRtcLibrary::State final {
public:
    State(std::string base_name,
          const WebRtcLibraryKind kind,
          std::shared_ptr<DynamicLibrary> library,
          std::shared_ptr<PxNetPlugin> compatibility_module)
        : base_name_(std::move(base_name)),
          kind_(kind),
          library_(std::move(library)),
          compatibility_module_(std::move(compatibility_module)) {}

    const std::string base_name_;
    const WebRtcLibraryKind kind_;
    const std::shared_ptr<DynamicLibrary> library_;
    const std::shared_ptr<PxNetPlugin> compatibility_module_;
};

WebRtcLibrary::WebRtcLibrary(std::shared_ptr<State> state)
    : state_(std::move(state)) {}

WebRtcLibrary::~WebRtcLibrary() = default;

WebRtcLibraryKind WebRtcLibrary::Kind() const {
    return state_->kind_;
}

std::string WebRtcLibrary::BaseName() const {
    return state_->base_name_;
}

WebRtcLibraryInfo WebRtcLibrary::Info() const {
    const auto& module = state_->compatibility_module_;
    return WebRtcLibraryInfo{
        .id = module->GetPluginId(),
        .name = module->GetPluginName(),
        .author = module->GetPluginAuthor(),
        .description = module->GetPluginDescription(),
        .version_name = module->GetVersionName(),
        .version_code = module->GetVersionCode(),
        .enabled = module->IsPluginEnabled(),
    };
}

bool WebRtcLibrary::Start(
    const WebRtcLibraryConfiguration& configuration) {
    return state_->compatibility_module_->OnCreate(
        MakeCompatibilityConfiguration(state_->base_name_, configuration));
}

void WebRtcLibrary::Stop() {
    static_cast<void>(state_->compatibility_module_->OnStop());
}

void WebRtcLibrary::Destroy() {
    static_cast<void>(state_->compatibility_module_->OnDestroy());
}

void WebRtcLibrary::SetEventCallback(WebRtcEventCallback callback) {
    state_->compatibility_module_->RegisterEventCallback(callback);
}

void WebRtcLibrary::SetEnabled(const bool enabled) {
    if (enabled) {
        state_->compatibility_module_->EnablePlugin();
    }
    else {
        state_->compatibility_module_->DisablePlugin();
    }
}

bool WebRtcLibrary::IsWorking() const {
    return state_->compatibility_module_->IsWorking();
}

void WebRtcLibrary::On1Second() {
    state_->compatibility_module_->On1Second();
}

void WebRtcLibrary::UpdateSettings(const WebRtcLibrarySettings& settings) {
    state_->compatibility_module_->OnSyncPluginSettingsInfo(
        MakeCompatibilitySettings(settings));
}

void WebRtcLibrary::DispatchAppEvent(
    const std::shared_ptr<AppBaseEvent>& event) {
    state_->compatibility_module_->DispatchAppEvent(event);
}

void WebRtcLibrary::UpdateD3DResources(
    const std::uint64_t adapter_uid,
    const Microsoft::WRL::ComPtr<ID3D11Device>& device,
    const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context) {
    state_->compatibility_module_->d3d11_devices_[adapter_uid] = device;
    state_->compatibility_module_->d3d11_devices_context_[adapter_uid] = context;
}

void WebRtcLibrary::ClearD3DResources(const std::uint64_t adapter_uid) {
    state_->compatibility_module_->d3d11_devices_.erase(adapter_uid);
    state_->compatibility_module_->d3d11_devices_context_.erase(adapter_uid);
}

void WebRtcLibrary::Send(
    const std::shared_ptr<Data>& message,
    const bool run_through) {
    state_->compatibility_module_->PostProtoMessage(message, run_through);
}

bool WebRtcLibrary::SendToStream(
    const std::string& stream_id,
    const std::shared_ptr<Data>& message,
    const bool run_through) {
    return state_->compatibility_module_->PostTargetStreamProtoMessage(
        stream_id, message, run_through);
}

FileTransferSendResult WebRtcLibrary::SendFileTransfer(
    const std::string& stream_id,
    const std::shared_ptr<Data>& message,
    const bool run_through,
    const std::string& connection_instance_id) {
    return state_->compatibility_module_->PostTargetFileTransferProtoMessage(
        stream_id, message, run_through, connection_instance_id);
}

void WebRtcLibrary::SubmitRawAudio(
    const std::shared_ptr<Data>& data,
    const int samples,
    const int channels,
    const int bits) {
    state_->compatibility_module_->OnRawAudioData(
        data, samples, channels, bits);
}

void WebRtcLibrary::SubmitEncodedVideo(
    const std::string& monitor_name,
    const PxPluginEncodedVideoType video_type,
    const std::shared_ptr<Data>& data,
    const std::uint64_t frame_index,
    const int frame_width,
    const int frame_height,
    const bool key_frame) {
    state_->compatibility_module_->OnEncodedVideoFrame(
        monitor_name, video_type, data, frame_index,
        frame_width, frame_height, key_frame);
}

void WebRtcLibrary::ApplyLogicalSessionCapabilities(
    const PxLogicalSessionCapabilityUpdate& update) {
    state_->compatibility_module_->ApplyLogicalSessionCapabilities(update);
}

int WebRtcLibrary::ConnectedClientCount() const {
    return state_->compatibility_module_->GetConnectedClientsCount();
}

int WebRtcLibrary::MediaConsumerCount() const {
    return state_->compatibility_module_->GetMediaConsumersCount();
}

bool WebRtcLibrary::HasVideoClient() const {
    return state_->compatibility_module_->IsWorking() &&
        !state_->compatibility_module_->IsOnlyAudioClients();
}

std::int64_t WebRtcLibrary::QueuedMediaMessageCount() const {
    return state_->compatibility_module_->GetQueuingMediaMsgCount();
}

std::int64_t WebRtcLibrary::QueuedFileTransferMessageCount() const {
    return state_->compatibility_module_->GetQueuingFtMsgCount();
}

std::vector<std::shared_ptr<PxConnectedClientInfo>>
WebRtcLibrary::ConnectedClients() const {
    return state_->compatibility_module_->GetConnectedClientInfo();
}

void WebRtcLibrary::SubmitLocalSharedTexture(
    const std::string& monitor_name,
    const std::uint64_t frame_index,
    const int frame_width,
    const int frame_height,
    const std::uint64_t shared_handle,
    const std::int64_t adapter_id,
    const std::uint64_t frame_format) {
    state_->compatibility_module_->OnRawVideoFrameSharedTexture(
        monitor_name, frame_index, frame_width, frame_height,
        shared_handle, adapter_id, frame_format);
}

void WebRtcLibrary::SubmitLocalYuv(
    const std::string& monitor_name,
    const std::uint64_t frame_index,
    const int frame_width,
    const int frame_height,
    const std::shared_ptr<Image>& image) {
    state_->compatibility_module_->OnRawVideoFrameYuv(
        monitor_name, frame_index, frame_width, frame_height, image);
}

void WebRtcLibrary::UpdateCaptureMonitorInfo(
    const CaptureMonitorInfoMessage& message) {
    state_->compatibility_module_->UpdateCaptureMonitorInfo(message);
}

void WebRtcLibrary::ApplyRemoteSdp(const MsgRtcRemoteSdp& message) {
    state_->compatibility_module_->ApplyRtcRemoteSdp(message);
}

void WebRtcLibrary::ApplyRemoteIce(const MsgRtcRemoteIce& message) {
    state_->compatibility_module_->ApplyRtcRemoteIce(message);
}

void WebRtcLibrary::DispatchLocalMessage(
    const std::shared_ptr<Message>& message) {
    state_->compatibility_module_->OnMessage(message);
}

PxLocalRtcAllocResult WebRtcLibrary::AllocateLocalInstance(
    const std::shared_ptr<PxLocalRtcRequestInfo>& request,
    std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>
        completion) {
    return state_->compatibility_module_->AllocNewLocalRtcInstance(
        request, std::move(completion));
}

bool WebRtcLibrary::SetVoiceAuthorization(
    const std::string& stream_id,
    const std::string& call_id,
    const bool authorized) {
    return state_->compatibility_module_->SetVoiceCallAuthorization(
        stream_id, call_id, authorized);
}

bool WebRtcLibrary::SubmitVoicePcm(
    const std::string& stream_id,
    const std::string& call_id,
    const std::shared_ptr<const std::vector<std::int16_t>>& samples,
    const int sample_rate,
    const int channels) {
    if (!samples || samples->empty()) {
        return false;
    }
    // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): synchronous libwebrtc PCM ABI
    state_->compatibility_module_->OnVoiceCallPcm(
        stream_id, call_id, samples->data(), samples->size(),
        sample_rate, channels);
    return true;
}

std::shared_ptr<WebRtcLibraryHost> WebRtcLibraryHost::Create(
    std::filesystem::path library_directory) {
    return std::make_shared<WebRtcLibraryHost>(
        std::move(library_directory));
}

WebRtcLibraryHost::WebRtcLibraryHost(
    std::filesystem::path library_directory)
    : library_directory_(std::move(library_directory)) {}

WebRtcLibraryHost::~WebRtcLibraryHost() {
    Reset();
}

std::vector<std::shared_ptr<WebRtcLibrary>> WebRtcLibraryHost::Load() {
    if (!loaded_libraries_.empty()) {
        return loaded_libraries_;
    }
    for (const auto& [base_name, kind] : {
             std::pair{std::string("net_rtc"), WebRtcLibraryKind::kRemote},
             std::pair{std::string("net_rtc_local"), WebRtcLibraryKind::kLocal}}) {
        if (auto library = LoadExact(base_name, kind)) {
            loaded_libraries_.push_back(std::move(library));
        }
    }
    return loaded_libraries_;
}

void WebRtcLibraryHost::Reset() {
    loaded_libraries_.clear();
}

std::shared_ptr<WebRtcLibrary> WebRtcLibraryHost::LoadExact(
    const std::string& base_name,
    const WebRtcLibraryKind kind) {
#if defined(_WIN32)
    const auto path = library_directory_ / (base_name + ".dll");
#else
    const auto path = library_directory_ / (base_name + ".so");
#endif
    if (!std::filesystem::is_regular_file(path)) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "code=TRANSPORT_RTC_LIBRARY_LOAD_FAILED operation=load "
             "outcome=failed recoverable=false library={} reason=missing_file",
             base_name);
        return {};
    }
    auto library = std::make_shared<DynamicLibrary>(path.wstring());
    if (!library->Load()) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "code=TRANSPORT_RTC_LIBRARY_LOAD_FAILED operation=load "
             "outcome=failed recoverable=false library={} reason=load_failed",
             base_name);
        return {};
    }
    const auto factory = reinterpret_cast<WebRtcFactory>(
        library->GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary): established WebRTC DLL symbol ABI
    if (!factory) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "code=TRANSPORT_RTC_LIBRARY_LOAD_FAILED operation=resolve_factory "
             "outcome=failed recoverable=false library={} reason=missing_factory",
             base_name);
        return {};
    }
    auto instance = static_cast<PxNetPlugin*>(factory()); // NOLINT(gammaray-raw-pointer-boundary): DLL singleton is borrowed and immediately lifetime-aliased
    if (!instance) {
        LOGE("event=webrtc.library.load component=webrtc_library_host "
             "code=TRANSPORT_RTC_LIBRARY_LOAD_FAILED operation=create "
             "outcome=failed recoverable=false library={} reason=null_instance",
             base_name);
        return {};
    }
    auto compatibility_module = std::shared_ptr<PxNetPlugin>(
        instance,
        [library](PxNetPlugin*) noexcept { // NOLINT(gammaray-raw-pointer-boundary): DLL owns singleton; captured RAII library governs unload
            static_cast<void>(library);
        });
    LOGI("event=webrtc.library.load component=webrtc_library_host "
         "library={} outcome=success",
         base_name);
    auto state = std::make_shared<WebRtcLibrary::State>(
        base_name, kind, library, std::move(compatibility_module));
    return std::make_shared<WebRtcLibrary>(std::move(state));
}

}  // namespace px
