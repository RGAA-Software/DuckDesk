#pragma once

#include <android/native_window.h>
#include <jni.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace px {
class MessageNotifier;
class MessageListener;
class ThunderSdk;
} // namespace px

namespace pixels::android {

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
    bool enable_video{true};
    bool enable_audio{true};
    bool enable_input{true};
};

class JavaSessionCallback final {
  public:
    static std::shared_ptr<JavaSessionCallback> Create(JNIEnv& environment, jobject listener);

    JavaSessionCallback(std::uintptr_t vm_handle, std::uintptr_t listener_handle);
    ~JavaSessionCallback();

    JavaSessionCallback(const JavaSessionCallback&) = delete;
    JavaSessionCallback& operator=(const JavaSessionCallback&) = delete;

    void Connected(const NativeSessionConfig& config, const std::string& active_monitor_name, bool supports_audio, bool supports_input,
                   bool supports_file_transfer, bool supports_clipboard) const;
    void FrameSizeChanged(const std::string& session_id, std::int32_t width, std::int32_t height) const;
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
    bool SendPointer(std::int32_t action, float x_ratio, float y_ratio);
    void Stop();

  private:
    NativeSessionConfig config_{};
    std::shared_ptr<JavaSessionCallback> callback_{};
    std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface_{};
    std::shared_ptr<px::MessageNotifier> message_notifier_{};
    std::shared_ptr<px::MessageListener> session_listener_{};
    std::shared_ptr<px::ThunderSdk> sdk_{};
    std::mutex command_mutex_{};
    std::mutex lifecycle_mutex_{};
    std::vector<std::unique_ptr<ANativeWindow, NativeWindowReleaser>> retired_surfaces_{};
    bool initialized_{};
    bool started_{};
    std::atomic_bool stopped_{};
    std::string client_signal_device_id_{};
    std::string active_monitor_name_{};
};

} // namespace pixels::android
