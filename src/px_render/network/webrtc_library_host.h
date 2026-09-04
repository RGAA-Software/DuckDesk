#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <d3d11.h>
#include <wrl/client.h>

#include "px_common_new/file_transfer_send_result.h"

namespace px {

class AppBaseEvent;
class CaptureMonitorInfoMessage;
class Data;
class Image;
class Message;
class MsgRtcRemoteIce;
class MsgRtcRemoteSdp;
class PxConnectedClientInfo;
class PxLocalRtcReplyInfo;
class PxLocalRtcRequestInfo;
class PxLogicalSessionCapabilityUpdate;
class PxPluginBaseEvent;
enum class PxLocalRtcAllocResult;
enum class PxPluginEncodedVideoType;

enum class WebRtcLibraryKind {
    kRemote,
    kLocal,
};

struct WebRtcLibraryConfiguration final {
    std::string base_path;
    std::wstring base_data_path;
    std::string capture_audio_device_id;
    std::string device_id;
    bool direct_allow_takeover{true};
    bool relay_enabled{true};
    int language{1};
    std::string appkey;
};

struct WebRtcLibrarySettings final {
    std::string device_id;
    std::string device_random_password;
    std::string device_safety_password;
    std::string relay_host;
    std::string relay_port;
    bool can_be_operated{true};
    bool direct_allow_takeover{true};
    bool relay_enabled{true};
    int language{1};
    bool file_transfer_enabled{true};
    bool audio_enabled{true};
    std::string appkey;
    std::uint64_t max_transmit_speed{0};
    std::uint64_t max_receive_speed{0};
    int role{1};
};

struct WebRtcLibraryInfo final {
    std::string id;
    std::string name;
    std::string author;
    std::string description;
    std::string version_name;
    std::uint32_t version_code{0};
    bool enabled{false};
};

using WebRtcEventCallback = std::function<void(
    const std::shared_ptr<PxPluginBaseEvent>&)>;

// Concrete network component backed by one fixed WebRTC dynamic library.
// Product code interacts only with this typed facade. The established DLL
// singleton ABI is confined to webrtc_library_host.cpp.
//
// Lifetime:
// - Owned by RenderModuleRegistry through shared_ptr.
// - State retains the DynamicLibrary for every in-flight facade operation.
// - Stop and Destroy run before the final library owner is released.
//
// Threading:
// - Lifecycle calls are serialized by RenderModuleRegistry.
// - Media and network calls follow the existing WebRTC library contract.
class WebRtcLibrary final {
private:
    class State;

public:
    explicit WebRtcLibrary(std::shared_ptr<State> state);
    ~WebRtcLibrary();

    WebRtcLibrary(const WebRtcLibrary&) = delete;
    WebRtcLibrary& operator=(const WebRtcLibrary&) = delete;

    [[nodiscard]] WebRtcLibraryKind Kind() const;
    [[nodiscard]] std::string BaseName() const;
    [[nodiscard]] WebRtcLibraryInfo Info() const;
    [[nodiscard]] bool Start(const WebRtcLibraryConfiguration& configuration);
    void Stop();
    void Destroy();
    void SetEventCallback(WebRtcEventCallback callback);
    void SetEnabled(bool enabled);
    [[nodiscard]] bool IsWorking() const;
    void On1Second();
    void UpdateSettings(const WebRtcLibrarySettings& settings);
    void DispatchAppEvent(const std::shared_ptr<AppBaseEvent>& event);
    void UpdateD3DResources(
        std::uint64_t adapter_uid,
        const Microsoft::WRL::ComPtr<ID3D11Device>& device,
        const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context);
    void ClearD3DResources(std::uint64_t adapter_uid);

    void Send(const std::shared_ptr<Data>& message, bool run_through);
    [[nodiscard]] bool SendToStream(
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        bool run_through);
    [[nodiscard]] FileTransferSendResult SendFileTransfer(
        const std::string& stream_id,
        const std::shared_ptr<Data>& message,
        bool run_through,
        const std::string& connection_instance_id = {});
    void SubmitRawAudio(
        const std::shared_ptr<Data>& data,
        int samples,
        int channels,
        int bits);
    void SubmitEncodedVideo(
        const std::string& monitor_name,
        PxPluginEncodedVideoType video_type,
        const std::shared_ptr<Data>& data,
        std::uint64_t frame_index,
        int frame_width,
        int frame_height,
        bool key_frame);
    void ApplyLogicalSessionCapabilities(
        const PxLogicalSessionCapabilityUpdate& update);
    [[nodiscard]] int ConnectedClientCount() const;
    [[nodiscard]] int MediaConsumerCount() const;
    [[nodiscard]] bool HasVideoClient() const;
    [[nodiscard]] std::int64_t QueuedMediaMessageCount() const;
    [[nodiscard]] std::int64_t QueuedFileTransferMessageCount() const;
    [[nodiscard]] std::vector<std::shared_ptr<PxConnectedClientInfo>>
    ConnectedClients() const;

    void SubmitLocalSharedTexture(
        const std::string& monitor_name,
        std::uint64_t frame_index,
        int frame_width,
        int frame_height,
        std::uint64_t shared_handle,
        std::int64_t adapter_id,
        std::uint64_t frame_format);
    void SubmitLocalYuv(
        const std::string& monitor_name,
        std::uint64_t frame_index,
        int frame_width,
        int frame_height,
        const std::shared_ptr<Image>& image);
    void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& message);
    void ApplyRemoteSdp(const MsgRtcRemoteSdp& message);
    void ApplyRemoteIce(const MsgRtcRemoteIce& message);
    void DispatchLocalMessage(const std::shared_ptr<Message>& message);
    [[nodiscard]] PxLocalRtcAllocResult AllocateLocalInstance(
        const std::shared_ptr<PxLocalRtcRequestInfo>& request,
        std::function<void(const std::shared_ptr<PxLocalRtcReplyInfo>&)>
            completion);
    [[nodiscard]] bool SetVoiceAuthorization(
        const std::string& stream_id,
        const std::string& call_id,
        bool authorized);
    [[nodiscard]] bool SubmitVoicePcm(
        const std::string& stream_id,
        const std::string& call_id,
        const std::shared_ptr<const std::vector<std::int16_t>>& samples,
        int sample_rate,
        int channels);

private:
    std::shared_ptr<State> state_;

    friend class WebRtcLibraryHost;
};

// Loads only the two fixed WebRTC network libraries. No discovery, generic
// transport interface, or plug-in object escapes this boundary.
class WebRtcLibraryHost final {
public:
    [[nodiscard]] static std::shared_ptr<WebRtcLibraryHost> Create(
        std::filesystem::path library_directory);

    explicit WebRtcLibraryHost(std::filesystem::path library_directory);
    ~WebRtcLibraryHost();

    WebRtcLibraryHost(const WebRtcLibraryHost&) = delete;
    WebRtcLibraryHost& operator=(const WebRtcLibraryHost&) = delete;

    [[nodiscard]] std::vector<std::shared_ptr<WebRtcLibrary>> Load();
    void Reset();

private:
    [[nodiscard]] std::shared_ptr<WebRtcLibrary> LoadExact(
        const std::string& base_name,
        WebRtcLibraryKind kind);

    std::filesystem::path library_directory_;
    std::vector<std::shared_ptr<WebRtcLibrary>> loaded_libraries_;
};

}  // namespace px
