#pragma once

#include "client_launch_config.h"
#include "client_video_frame.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace px {
class Data;
class MessageListener;
class MessageNotifier;
class ThunderSdk;
class RecordingSession;
class SdkStatistics;
class VoiceCallController;
class NetClient;
struct WindowsVideoResources;
struct VoiceCallStatus;
namespace ft {
class FtAsyncSession;
struct TransferJobStatus;
} // namespace ft
namespace rdp {
class RdpClientEndpoint;
class RdpSession;
struct DesktopFrame;
} // namespace rdp
} // namespace px

namespace px::client::imgui {

class ClientAudioOutput;

enum class ClientConnectionState : std::uint8_t { Connecting, Connected, MediaUnavailable, Rejected, Disconnected };

enum class ClientConnectionFailure : std::uint8_t { None, Authorization, Occupied, SessionPolicy, TakenOver, Transport };

struct ClientResolution final {
    int width{};
    int height{};
};

struct ClientRemoteCursor final {
    bool received{};
    bool visible{true};
    std::uint32_t type{};
};

struct ClientSessionSnapshot final {
    ClientConnectionState state{ClientConnectionState::Connecting};
    ClientConnectionFailure failure{ClientConnectionFailure::None};
    std::string status{};
    std::string monitorName{};
    std::shared_ptr<ClientVideoFrame> frame{};
    std::vector<std::string> monitors{};
    std::vector<ClientResolution> resolutions{};
    int framesPerSecond{};
    int latencyMilliseconds{};
    int bitrateKbps{};
    std::string decoder{};
    bool fileTransferAvailable{};
    bool voiceAvailable{};
    bool recording{};
    bool virtualDisplayAvailable{};
    std::uint32_t virtualDisplayCount{};
    std::uint32_t virtualDisplayMaximum{};
    bool virtualDisplayBusy{};
    std::string voiceStatus{};
    ClientRemoteCursor remoteCursor{};
};

struct ClientTransferJob final {
    std::int32_t id{};
    std::uint64_t totalBytes{};
    std::uint64_t completedBytes{};
    double bytesPerSecond{};
    std::int32_t fileNumber{};
    std::int32_t fileCount{};
    std::string name{};
    std::string sourcePath{};
    std::string destinationDirectory{};
    bool download{};
    bool done{};
    std::string error{};
};

struct ClientRemoteEntry final {
    std::string name{};
    std::string path{};
    std::uint64_t size{};
    std::uint64_t modifiedTime{};
    bool directory{};
    bool hidden{};
};

struct ClientOverwriteRequest final {
    std::int32_t jobId{};
    std::int32_t fileNumber{};
    std::string path{};
    bool upload{};
    bool identical{};
};

struct ClientFileOperationResult final {
    bool success{};
    std::string error{};
};

class ClientSession final : public std::enable_shared_from_this<ClientSession> {
  public:
    static std::shared_ptr<ClientSession> Create(ClientLaunchConfig config, std::shared_ptr<px::WindowsVideoResources> videoResources);
    ClientSession(ClientLaunchConfig config, std::shared_ptr<px::WindowsVideoResources> videoResources);
    ~ClientSession();

    bool Initialize();
    void Start();
    void Stop();
    [[nodiscard]] ClientSessionSnapshot Snapshot() const;
    bool SendMouseMove(float xRatio, float yRatio);
    bool SendMouseButton(std::uint8_t button, bool down, float xRatio, float yRatio);
    bool SendMouseWheel(float horizontal, float vertical);
    bool SendKey(std::uint32_t virtualKey, std::uint32_t scanCode, bool down);
    bool SendText(const std::string& text);
    bool SendClipboardText(const std::string& text);
    [[nodiscard]] std::optional<std::string> TakeRemoteClipboardText();
    bool SendSecureAttention();
    bool SwitchMonitor(const std::string& name);
    bool ChangeResolution(int width, int height);
    bool SetFrameRate(int frameRate);
    bool SetAudioEnabled(bool enabled);
    bool CreateVirtualDisplay();
    bool RemoveVirtualDisplay();
    [[nodiscard]] std::optional<std::string> SaveScreenshot() const;
    [[nodiscard]] std::vector<ClientTransferJob> TransferJobs() const;
    [[nodiscard]] std::vector<ClientRemoteEntry> RemoteEntries() const;
    [[nodiscard]] std::vector<ClientRemoteEntry> RemoteLocations() const;
    [[nodiscard]] std::string RemotePath() const;
    [[nodiscard]] std::optional<ClientOverwriteRequest> PendingOverwrite() const;
    bool ListRemoteDirectory(const std::string& path, bool includeHidden = false);
    std::int32_t StartUpload(const std::string& localPath, const std::string& remoteDirectory);
    std::int32_t StartDownload(const std::string& remotePath, const std::string& localDirectory);
    bool CancelTransfer(std::int32_t jobId);
    bool ResumeTransfer(std::int32_t jobId);
    void RemoveCompletedTransfers();
    bool ConfirmOverwrite(bool overwrite, bool applyToAll);
    bool CreateRemoteDirectory(const std::string& path);
    bool RemoveRemoteEntry(const std::string& path, bool directory);
    bool RemoveRemoteEntries(const std::vector<ClientRemoteEntry>& entries);
    bool RenameRemoteEntry(const std::string& path, const std::string& newName);
    [[nodiscard]] std::optional<ClientFileOperationResult> TakeRemoteFileOperationResult();
    bool StartRecording();
    bool StopRecording();
    bool StartVoiceCall();
    bool StopVoiceCall();
    bool SetVoiceMicrophoneMuted(bool muted);
    bool SetVoiceSpeakerMuted(bool muted);

  private:
    [[nodiscard]] bool SendMedia(const std::shared_ptr<px::Data>& data) const;
    bool InitializeRdp();
    void StartRdpProtocol(std::uint16_t loopbackPort);
    void ApplyRdpFrame(const std::shared_ptr<const px::rdp::DesktopFrame>& frame);
    [[nodiscard]] std::shared_ptr<px::ft::FtAsyncSession> FileTransfer() const;
    [[nodiscard]] std::shared_ptr<px::VoiceCallController> VoiceCall() const;
    void SetState(ClientConnectionState state, std::string status, ClientConnectionFailure failure = ClientConnectionFailure::None);

    ClientLaunchConfig config_{};
    std::shared_ptr<px::WindowsVideoResources> videoResources_{};
    std::shared_ptr<px::MessageNotifier> notifier_{};
    std::shared_ptr<px::MessageListener> listener_{};
    std::shared_ptr<px::ThunderSdk> sdk_{};
    std::shared_ptr<px::NetClient> rdpNetwork_{};
    std::shared_ptr<px::rdp::RdpClientEndpoint> rdpEndpoint_{};
    std::shared_ptr<px::rdp::RdpSession> rdpSession_{};
    std::unique_ptr<ClientAudioOutput> audio_{};
    std::shared_ptr<px::ft::FtAsyncSession> fileTransfer_{};
    std::shared_ptr<px::RecordingSession> recording_{};
    std::vector<std::shared_ptr<px::RecordingSession>> finishingRecordings_{};
    std::shared_ptr<px::VoiceCallController> voiceCall_{};
    std::shared_ptr<px::SdkStatistics> statistics_{};
    mutable std::mutex mutex_{};
    ClientConnectionState state_{ClientConnectionState::Connecting};
    ClientConnectionFailure failure_{ClientConnectionFailure::None};
    std::string status_{"Connecting"};
    std::string monitorName_{};
    std::vector<std::string> monitors_{};
    std::vector<ClientResolution> resolutions_{};
    std::shared_ptr<ClientVideoFrame> latestFrame_{};
    std::vector<std::uint8_t> rdpFrameBuffer_{};
    int rdpWidth_{};
    int rdpHeight_{};
    std::vector<ClientTransferJob> transferJobs_{};
    std::vector<ClientRemoteEntry> remoteEntries_{};
    std::vector<ClientRemoteEntry> remoteLocations_{};
    std::string remotePath_{};
    std::optional<ClientOverwriteRequest> overwrite_{};
    std::optional<ClientFileOperationResult> remoteFileOperationResult_{};
    std::optional<std::string> remoteClipboardText_{};
    std::string recordingId_{};
    std::string voiceStatus_{};
    std::string virtualDisplayRequestId_{};
    int decodedFrames_{};
    int framesPerSecond_{};
    int latencyMilliseconds_{};
    int bitrateKbps_{};
    std::int64_t lastReceivedBytes_{};
    std::chrono::steady_clock::time_point statisticsStarted_{std::chrono::steady_clock::now()};
    bool fileTransferAvailable_{};
    bool voiceAvailable_{};
    bool audioEnabled_{true};
    bool virtualDisplayAvailable_{};
    std::uint32_t virtualDisplayCount_{};
    std::uint32_t virtualDisplayMaximum_{};
    ClientRemoteCursor remoteCursor_{};
    float cursorX_{0.5F};
    float cursorY_{0.5F};
    std::atomic_bool started_{};
    std::atomic_bool stopped_{};
};

} // namespace px::client::imgui
