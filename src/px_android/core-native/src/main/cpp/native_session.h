#pragma once

#include <android/native_window.h>
#include <jni.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace px {
class MessageNotifier;
class MessageListener;
class SdkStatistics;
class ThunderSdk;
class Thread;
class RecordWriter;
class FileDirectory;
} // namespace px

namespace px::ft {
class FtAsyncSession;
struct TransferJobStatus;
} // namespace px::ft

namespace pixels::android {

class NativeAudioPlayer;
class NativeClipboard;
class NativeVoiceCall;
struct NativeClipboardFile;
struct NativeClipboardFiles;
struct NativeVoiceCallStatus;

struct NativeSessionConfig final {
    std::string session_id{};
    std::string host{};
    std::int32_t port{};
    bool ssl{};
    std::string remote_device_id{};
    std::string display_name{};
    std::string stream_id{};
    std::string client_device_id{};
    std::string random_password{};
    std::string connection_ticket{};
    std::string connection_nonce{};
    std::string rtc_ice_config_json{};
    std::string relay_host{};
    std::int32_t relay_port{};
    std::int32_t network_type{};
    bool enable_video{true};
    bool enable_audio{true};
    bool enable_input{true};
    bool enable_clipboard{true};
};

struct NativeGamepadState final {
    std::int32_t buttons{};
    std::int32_t left_trigger{};
    std::int32_t right_trigger{};
    std::int32_t left_thumb_x{};
    std::int32_t left_thumb_y{};
    std::int32_t right_thumb_x{};
    std::int32_t right_thumb_y{};
};

class JavaSessionCallback final {
  public:
    static std::shared_ptr<JavaSessionCallback> Create(JNIEnv& environment, jobject listener);

    JavaSessionCallback(std::uintptr_t vm_handle, std::uintptr_t listener_handle);
    ~JavaSessionCallback();

    JavaSessionCallback(const JavaSessionCallback&) = delete;
    JavaSessionCallback& operator=(const JavaSessionCallback&) = delete;

    void Connected(const NativeSessionConfig& config, const std::vector<std::string>& monitor_names, const std::string& active_monitor_name,
                   bool supports_audio, bool supports_input, bool supports_file_transfer, bool supports_clipboard, bool supports_virtual_displays,
                   std::int32_t owned_virtual_display_count, std::int32_t maximum_virtual_display_count, std::int64_t topology_generation,
                   bool supports_voice_call, bool voice_call_requires_headset) const;
    void MonitorsChanged(const std::string& session_id, const std::vector<std::string>& monitor_names, const std::string& active_monitor_name) const;
    void VirtualDisplayResult(const std::string& session_id, const std::string& request_id, bool accepted, std::int32_t state, bool topology_changed,
                              std::int64_t topology_generation, std::int32_t owned_display_count, const std::string& error_code,
                              const std::string& error_message) const;
    void FrameSizeChanged(const std::string& session_id, std::int32_t width, std::int32_t height) const;
    void Statistics(const std::string& session_id, std::int32_t frames_per_second, std::int32_t latency_millis, std::int32_t bitrate_kbps) const;
    void GamepadRumble(const std::string& session_id, std::int32_t strong_motor, std::int32_t weak_motor) const;
    void ClipboardText(const std::string& session_id, const std::string& text) const;
    void ClipboardFiles(const std::string& session_id, const NativeClipboardFiles& files) const;
    void ClipboardFilesReady(const std::string& session_id, const std::string& generation, const std::vector<std::string>& paths,
                             const std::string& error) const;
    void FileTransferProgress(const std::string& session_id, const px::ft::TransferJobStatus& status) const;
    void FileTransferDone(const std::string& session_id, std::int32_t job_id, const std::string& error) const;
    void FileTransferOverwrite(const std::string& session_id, std::int32_t job_id, std::int32_t file_number, const std::string& path, bool upload,
                               bool identical) const;
    void RemoteDirectory(const std::string& session_id, const px::FileDirectory& directory) const;
    void RecordingState(const std::string& session_id, const std::string& recording_id, std::int32_t state, const std::string& error) const;
    void VoiceCallState(const std::string& session_id, const NativeVoiceCallStatus& status) const;
    void Disconnected(const std::string& session_id, std::int32_t reason, bool recoverable) const;

  private:
    std::uintptr_t vm_handle_{};
    std::uintptr_t listener_handle_{};
};

struct NativeWindowReleaser final {
    void operator()(ANativeWindow* window) const noexcept; // NOLINT(gammaray-raw-pointer-boundary)
};

class NativeSession final : public std::enable_shared_from_this<NativeSession> {
  public:
    static std::shared_ptr<NativeSession> Create(NativeSessionConfig config, std::shared_ptr<JavaSessionCallback> callback,
                                                 std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface);

    NativeSession(NativeSessionConfig config, std::shared_ptr<JavaSessionCallback> callback,
                  std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface);
    ~NativeSession();

    NativeSession(const NativeSession&) = delete;
    NativeSession& operator=(const NativeSession&) = delete;

    bool Initialize();
    bool Start();
    bool RebindSurface(std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface);
    bool DetachSurface();
    bool SendMouse(std::int32_t action, std::int32_t button, bool down, float x_ratio, float y_ratio, std::int32_t delta_x, std::int32_t delta_y);
    bool SendKey(std::int32_t virtual_key_code, bool down);
    bool SendGamepad(const NativeGamepadState& state);
    bool SendText(const std::string& text);
    bool SendClipboardText(const std::string& text);
    bool SendClipboardFiles(const std::string& generation, std::vector<NativeClipboardFile> files);
    bool DownloadClipboardFiles(const std::string& generation, const std::string& destination_directory);
    std::int32_t StartFileUpload(const std::string& local_path, const std::string& remote_directory);
    std::int32_t StartFileDownload(const std::string& remote_path, const std::string& local_directory);
    bool ListRemoteDirectory(const std::string& remote_path);
    bool CancelFileTransfer(std::int32_t job_id);
    bool ConfirmFileOverwrite(std::int32_t job_id, std::int32_t file_number, bool overwrite, std::uint64_t offset_bytes, bool apply_to_all);
    bool StartRecording(const std::string& recording_id, const std::string& staging_directory);
    bool StopRecording(const std::string& recording_id);
    bool StartVoiceCall();
    bool StopVoiceCall();
    bool SetVoiceMicrophoneMuted(bool muted);
    bool SetVoiceSpeakerMuted(bool muted);
    bool SendSecureAttention();
    bool SwitchMonitor(const std::string& monitor_name);
    bool RequestVirtualDisplay(const std::string& request_id, std::int32_t operation, std::int32_t width, std::int32_t height,
                               std::int32_t refresh_hz);
    bool SetAudioEnabled(bool enabled);
    void Stop();

  private:
    bool QueueSurfaceUpdate(std::shared_ptr<ANativeWindow> surface);
    void CompleteSurfaceUpdate();
    void DispatchSurfaceUpdate(std::shared_ptr<px::ThunderSdk> sdk, std::shared_ptr<ANativeWindow> retiring_surface, std::uintptr_t surface_handle);

    NativeSessionConfig config_{};
    std::shared_ptr<JavaSessionCallback> callback_{};
    std::shared_ptr<ANativeWindow> surface_{};
    std::shared_ptr<ANativeWindow> pending_surface_{};
    std::shared_ptr<px::MessageNotifier> message_notifier_{};
    std::shared_ptr<px::MessageListener> session_listener_{};
    std::shared_ptr<px::ThunderSdk> sdk_{};
    std::shared_ptr<px::ft::FtAsyncSession> file_transfer_session_{};
    std::shared_ptr<px::Thread> recording_thread_{};
    std::shared_ptr<px::RecordWriter> recording_writer_{};
    std::shared_ptr<NativeClipboard> clipboard_{};
    std::shared_ptr<NativeVoiceCall> voice_call_{};
    std::shared_ptr<px::SdkStatistics> statistics_{};
    std::unique_ptr<NativeAudioPlayer> audio_player_{};
    std::mutex command_mutex_{};
    std::mutex lifecycle_mutex_{};
    bool initialized_{};
    bool started_{};
    std::atomic_bool file_transfer_ready_{};
    std::atomic_uint64_t active_recording_generation_{};
    std::atomic_uint64_t recording_video_packets_{};
    std::atomic_uint64_t recording_audio_packets_{};
    std::uint64_t next_recording_generation_{};
    std::uint64_t recording_writer_generation_{};
    std::string active_recording_id_{};
    bool surface_update_in_progress_{};
    bool has_pending_surface_update_{};
    float virtual_cursor_x_{0.5F};
    float virtual_cursor_y_{0.5F};
    std::atomic_bool stopped_{};
    std::string client_signal_device_id_{};
    std::string active_monitor_name_{};
    std::vector<std::string> monitor_names_{};
    std::int32_t last_video_width_{};
    std::int32_t last_video_height_{};
    std::int32_t decoded_frames_in_window_{};
    std::int64_t last_received_bytes_{};
    std::atomic_int32_t latest_latency_millis_{};
    std::chrono::steady_clock::time_point statistics_window_started_{std::chrono::steady_clock::now()};
};

} // namespace pixels::android
