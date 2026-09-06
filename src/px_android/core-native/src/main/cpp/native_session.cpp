#include "native_session.h"

#include <android/native_window_jni.h>

#include <format>
#include <functional>
#include <utility>

#include "px_client_sdk/gl/raw_image.h"
#include "px_client_sdk/sdk_decoder_render_type.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/md5.h"
#include "px_common/message_notifier.h"
#include "px_message/proto_message_maker.h"

namespace pixels::android {
namespace {

void WithEnvironment(const std::uintptr_t vm_handle, const std::function<void(JNIEnv&)>& action) {
    auto* vm = reinterpret_cast<JavaVM*>(vm_handle); // NOLINT(gammaray-raw-pointer-boundary)
    if (vm == nullptr) {
        return;
    }
    JNIEnv* environment = nullptr; // NOLINT(gammaray-raw-pointer-boundary)
    bool detach_when_done{};
    const auto environment_result = vm->GetEnv(reinterpret_cast<void**>(&environment), JNI_VERSION_1_6);
    if (environment_result == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&environment, nullptr) != JNI_OK) {
            return;
        }
        detach_when_done = true;
    } else if (environment_result != JNI_OK) {
        return;
    }
    action(*environment);
    if (environment->ExceptionCheck()) {
        environment->ExceptionClear();
    }
    if (detach_when_done) {
        vm->DetachCurrentThread();
    }
}

void DeleteLocalReference(JNIEnv& environment, const std::uintptr_t handle) {
    if (handle != 0U) {
        environment.DeleteLocalRef(reinterpret_cast<jobject>(handle));
    }
}

} // namespace

std::shared_ptr<JavaSessionCallback> JavaSessionCallback::Create(JNIEnv& environment, const jobject listener) {
    JavaVM* vm = nullptr; // NOLINT(gammaray-raw-pointer-boundary)
    if (environment.GetJavaVM(&vm) != JNI_OK) {
        return {};
    }
    const auto listener_handle = reinterpret_cast<std::uintptr_t>(environment.NewGlobalRef(listener));
    if (listener_handle == 0U) {
        return {};
    }
    return std::make_shared<JavaSessionCallback>(reinterpret_cast<std::uintptr_t>(vm), listener_handle);
}

JavaSessionCallback::JavaSessionCallback(const std::uintptr_t vm_handle, const std::uintptr_t listener_handle)
    : vm_handle_(vm_handle), listener_handle_(listener_handle) {}

JavaSessionCallback::~JavaSessionCallback() {
    const auto listener_handle = std::exchange(listener_handle_, 0U);
    WithEnvironment(vm_handle_, [listener_handle](JNIEnv& environment) {
        if (listener_handle != 0U) {
            environment.DeleteGlobalRef(reinterpret_cast<jobject>(listener_handle));
        }
    });
}

void JavaSessionCallback::Connected(const NativeSessionConfig& config, const std::string& active_monitor_name, const bool supports_audio,
                                    const bool supports_input, const bool supports_file_transfer, const bool supports_clipboard) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onConnected", "(Ljava/lang/String;Ljava/lang/String;ZZZZ)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(config.session_id.c_str()));
        const auto monitor_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(active_monitor_name.c_str()));
        if (method != nullptr && session_id_handle != 0U && monitor_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), reinterpret_cast<jstring>(monitor_handle),
                                       supports_audio, supports_input, supports_file_transfer, supports_clipboard);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, monitor_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::FrameSizeChanged(const std::string& session_id, const std::int32_t width, const std::int32_t height) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onFrameSizeChanged", "(Ljava/lang/String;II)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        if (method != nullptr && session_id_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), width, height);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::Disconnected(const std::string& session_id, const std::int32_t reason, const bool recoverable) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onDisconnected", "(Ljava/lang/String;IZ)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        if (method != nullptr && session_id_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), reason, recoverable);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void NativeWindowReleaser::operator()(ANativeWindow* window) const noexcept { // NOLINT(gammaray-raw-pointer-boundary)
    if (window != nullptr) {
        ANativeWindow_release(window);
    }
}

std::shared_ptr<NativeSession> NativeSession::Create(NativeSessionConfig config, std::shared_ptr<JavaSessionCallback> callback,
                                                     std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface) {
    auto session = std::make_shared<NativeSession>(std::move(config), std::move(callback), std::move(surface));
    if (!session->Initialize()) {
        return {};
    }
    return session;
}

NativeSession::NativeSession(NativeSessionConfig config, std::shared_ptr<JavaSessionCallback> callback,
                             std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface)
    : config_(std::move(config)), callback_(std::move(callback)), surface_(std::move(surface)) {}

NativeSession::~NativeSession() {
    Stop();
}

bool NativeSession::Initialize() {
    if (initialized_ || config_.session_id.empty() || config_.host.empty() || config_.port <= 0 || config_.remote_device_id.empty() ||
        config_.stream_id.empty() || config_.client_device_id.empty() || !surface_ || !callback_) {
        return false;
    }

    message_notifier_ = std::make_shared<px::MessageNotifier>();
    sdk_ = px::ThunderSdk::Make(message_notifier_);
    session_listener_ = message_notifier_->CreateListener(px::MessageExecutionLane::kControl);
    auto params = std::make_shared<px::ThunderSdkParams>();
    params->ssl_ = config_.ssl;
    params->enable_audio_ = false;
    params->enable_video_ = config_.enable_video;
    params->enable_controller_ = config_.enable_input;
    params->file_transfer_only_ = false;
    params->ip_ = config_.host;
    params->port_ = config_.port;
    params->udp_port_ = 20371;
    params->client_type_ = px::ClientType::kAndroid;
    params->nt_type_ = px::ClientNetworkType::kWebsocket;
    params->bare_device_id_ = config_.client_device_id;
    params->bare_remote_device_id_ = config_.remote_device_id;
    params->device_id_ = std::format("client_{}_{}", config_.client_device_id, px::MD5::Hex(config_.remote_device_id));
    client_signal_device_id_ = params->device_id_;
    params->remote_device_id_ = std::format("server_{}", config_.remote_device_id);
    params->ft_device_id_ = "ft_" + params->device_id_;
    params->ft_remote_device_id_ = "ft_" + params->remote_device_id_;
    params->stream_id_ = config_.stream_id;
    params->stream_name_ = config_.display_name;
    params->device_name_ = "Pixels Android";
    params->display_name_ = "Pixels Android";
    params->display_remote_name_ = config_.remote_device_id;
    params->media_path_ = std::format("/media?only_audio=0&remote_device_id={}&stream_id={}&visitor_device_id={}", config_.remote_device_id,
                                      config_.stream_id, config_.client_device_id);
    params->ft_path_ = std::format("/file/transfer?remote_device_id={}&stream_id={}&visitor_device_id={}", config_.remote_device_id,
                                   config_.stream_id, config_.client_device_id);
    params->enable_p2p_ = false;
    params->remote_device_random_pwd_ = config_.random_password;
    params->connection_ticket_ = config_.connection_ticket;
    params->connection_nonce_ = config_.connection_nonce;
    params->rtc_ice_config_json_ = config_.rtc_ice_config_json;
    params->relay_host_ = config_.relay_host;
    params->relay_port_ = config_.relay_port;
    params->render_type_name_ = "mediacodec_surface";

    const auto weak_self = weak_from_this();
    session_listener_->Listen<px::SdkMsgNetworkDisConnected>([weak_self](const auto&) {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            self->callback_->Disconnected(self->config_.session_id, 3, true);
        }
    });
    session_listener_->Listen<px::SdkMsgWsConnectionRejected>([weak_self](const auto&) {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            self->callback_->Disconnected(self->config_.session_id, 1, false);
        }
    });
    session_listener_->Listen<px::SdkMsgConnectionTakenOver>([weak_self](const auto&) {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            self->callback_->Disconnected(self->config_.session_id, 6, false);
        }
    });
    session_listener_->Listen<px::SdkMsgRelayRemoteDeviceOffline>([weak_self](const auto&) {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            self->callback_->Disconnected(self->config_.session_id, 2, false);
        }
    });
    sdk_->SetOnServerConfigurationCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load()) {
            return;
        }
        const auto& server_config = message->config();
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            self->active_monitor_name_ = server_config.capturing_monitor_name();
        }
        self->callback_->Connected(self->config_, server_config.capturing_monitor_name(), false, self->config_.enable_input, false, false);
    });
    sdk_->SetOnVideoFrameDecodedCallback([weak_self](std::shared_ptr<px::RawImage> image, const px::SdkCaptureMonitorInfo&) {
        const auto self = weak_self.lock();
        if (!self || !image || self->stopped_.load()) {
            return;
        }
        self->callback_->FrameSizeChanged(self->config_.session_id, image->img_width, image->img_height);
    });

    initialized_ = sdk_->Init(params, reinterpret_cast<void*>(surface_.get()), DecoderRenderType::kMediaCodecSurface);
    return initialized_;
}

bool NativeSession::Start() {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (!initialized_ || stopped_.load()) {
            return false;
        }
        if (started_) {
            return true;
        }
        started_ = true;
        sdk = sdk_;
    }
    sdk->Start();
    return true;
}

bool NativeSession::RebindSurface(std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    std::uintptr_t surface_handle{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (!surface || stopped_.load() || !sdk_) {
            return false;
        }
        retired_surfaces_.push_back(std::move(surface_));
        surface_ = std::move(surface);
        sdk = sdk_;
        surface_handle = reinterpret_cast<std::uintptr_t>(surface_.get()); // NOLINT(gammaray-raw-pointer-boundary)
    }
    sdk->UpdateRenderSurface(surface_handle);
    return true;
}

bool NativeSession::SendPointer(const std::int32_t action, const float x_ratio, const float y_ratio) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    std::string monitor_name;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_ || action < 0 || action > 2 || x_ratio < 0.0F || x_ratio > 1.0F ||
            y_ratio < 0.0F || y_ratio > 1.0F) {
            return false;
        }
        sdk = sdk_;
        monitor_name = active_monitor_name_;
    }
    if (monitor_name.empty())
        return false;
    const auto message =
        px::ProtoMessageMaker::MakeMouseEventFromTouch(action, monitor_name, x_ratio, y_ratio, client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

void NativeSession::Stop() {
    std::lock_guard command_lock(command_mutex_);
    if (stopped_.exchange(true)) {
        return;
    }
    std::shared_ptr<px::ThunderSdk> sdk;
    std::unique_ptr<ANativeWindow, NativeWindowReleaser> surface;
    std::vector<std::unique_ptr<ANativeWindow, NativeWindowReleaser>> retired_surfaces;
    {
        std::lock_guard lock(lifecycle_mutex_);
        sdk = std::move(sdk_);
        session_listener_.reset();
        message_notifier_.reset();
        surface = std::move(surface_);
        retired_surfaces = std::move(retired_surfaces_);
    }
    if (sdk)
        sdk->Exit();
}

} // namespace pixels::android
