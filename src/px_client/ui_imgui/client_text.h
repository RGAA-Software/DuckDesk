#pragma once

#include <cstdint>
#include <string_view>

namespace px::client::imgui {

enum class ClientText : std::uint8_t {
    Controls,
    Resolution,
    VirtualDisplays,
    Files,
    SecureAttention,
    Audio,
    StopRecording,
    Record,
    Screenshot,
    ScreenshotFailed,
    HangUp,
    Voice,
    MuteMicrophone,
    MuteSpeaker,
    Statistics,
    Hide,
    FileTransfer,
    RemotePath,
    Open,
    LocalPath,
    UploadLocalPath,
    DownloadSelection,
    RemoteFiles,
    Name,
    Type,
    Size,
    Folder,
    File,
    Transfers,
    Download,
    Upload,
    Cancel,
    DestinationExists,
    ApplyToAll,
    Overwrite,
    Skip,
    ConnectionRejected,
    AuthorizationRejected,
    DeviceOccupied,
    SessionPolicyRejected,
    SessionTakenOver,
    TransportRejected,
    ConnectionFailed,
    Ok,
    Light,
    Dark,
    Fullscreen,
    WaitingForFrame,
    Connecting,
    Connected,
    MediaUnavailable,
    Rejected,
    Disconnected,
    MediaUnavailableDetail,
    DisconnectedDetail,
    Count,
};

[[nodiscard]] std::string_view ClientTextValue(ClientText id, bool english) noexcept;

} // namespace px::client::imgui
