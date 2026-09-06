#include "native_session.h"

#include "native_audio_player.h"
#include "native_voice_call.h"

#include <android/native_window_jni.h>

#include <chrono>
#include <algorithm>
#include <cmath>
#include <format>
#include <functional>
#include <future>
#include <optional>
#include <utility>

#include "ft_async_session.h"
#include "ft_engine.h"
#include "px_client_sdk/gl/raw_image.h"
#include "px_client_sdk/sdk_decoder_render_type.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/md5.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_common/thread.h"
#include "px_common/time_util.h"
#include "px_media_record/record_writer.h"
#include "px_message/proto_message_maker.h"
#include "px_message/proto_converter.h"

namespace pixels::android {
namespace {

constexpr std::int32_t kMouseMoveAbsolute = 0;
constexpr std::int32_t kMouseMoveRelative = 1;
constexpr std::int32_t kMouseButton = 2;
constexpr std::int32_t kMouseWheel = 3;
constexpr std::int32_t kRecordingStarted = 1;
constexpr std::int32_t kRecordingCompleted = 2;
constexpr std::int32_t kRecordingFailed = 3;

std::int32_t MouseButtonFlag(const std::int32_t button, const bool down) {
    switch (button) {
    case 0:
        return down ? px::ButtonFlag::kLeftMouseButtonDown : px::ButtonFlag::kLeftMouseButtonUp;
    case 1:
        return down ? px::ButtonFlag::kMiddleMouseButtonDown : px::ButtonFlag::kMiddleMouseButtonUp;
    case 2:
        return down ? px::ButtonFlag::kRightMouseButtonDown : px::ButtonFlag::kRightMouseButtonUp;
    default:
        return px::ButtonFlag::kNone;
    }
}

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

std::uintptr_t MakeStringArray(JNIEnv& environment, const std::vector<std::string>& values) {
    const auto string_class_handle = reinterpret_cast<std::uintptr_t>(environment.FindClass("java/lang/String"));
    if (string_class_handle == 0U)
        return 0U;
    const auto array_handle = reinterpret_cast<std::uintptr_t>(
        environment.NewObjectArray(static_cast<jsize>(values.size()), reinterpret_cast<jclass>(string_class_handle), nullptr));
    for (std::size_t index = 0; array_handle != 0U && index < values.size(); ++index) {
        const auto value_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(values[index].c_str()));
        if (value_handle != 0U) {
            environment.SetObjectArrayElement(reinterpret_cast<jobjectArray>(array_handle), static_cast<jsize>(index),
                                              reinterpret_cast<jstring>(value_handle));
            DeleteLocalReference(environment, value_handle);
        }
    }
    DeleteLocalReference(environment, string_class_handle);
    return array_handle;
}

std::uintptr_t MakeByteArray(JNIEnv& environment, const std::string& value) {
    const auto result_handle = reinterpret_cast<std::uintptr_t>(environment.NewByteArray(static_cast<jsize>(value.size())));
    if (result_handle == 0U || value.empty()) {
        return result_handle;
    }
    environment.SetByteArrayRegion(reinterpret_cast<jbyteArray>(result_handle), 0, static_cast<jsize>(value.size()),
                                   reinterpret_cast<const jbyte*>(value.data())); // NOLINT(gammaray-raw-pointer-boundary)
    return environment.ExceptionCheck() ? 0U : result_handle;
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

void JavaSessionCallback::Connected(const NativeSessionConfig& config, const std::vector<std::string>& monitor_names,
                                    const std::string& active_monitor_name, const bool supports_audio, const bool supports_input,
                                    const bool supports_file_transfer, const bool supports_clipboard, const bool supports_virtual_displays,
                                    const std::int32_t owned_virtual_display_count, const std::int32_t maximum_virtual_display_count,
                                    const std::int64_t topology_generation, const bool supports_voice_call,
                                    const bool voice_call_requires_headset) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method =
            environment.GetMethodID(listener_class, "onConnected", "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;ZZZZZIIJZZ)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(config.session_id.c_str()));
        const auto monitor_names_handle = MakeStringArray(environment, monitor_names);
        const auto monitor_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(active_monitor_name.c_str()));
        if (method != nullptr && session_id_handle != 0U && monitor_names_handle != 0U && monitor_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle),
                                       reinterpret_cast<jobjectArray>(monitor_names_handle), reinterpret_cast<jstring>(monitor_handle),
                                       supports_audio, supports_input, supports_file_transfer, supports_clipboard, supports_virtual_displays,
                                       owned_virtual_display_count, maximum_virtual_display_count, topology_generation, supports_voice_call,
                                       voice_call_requires_headset);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, monitor_names_handle);
        DeleteLocalReference(environment, monitor_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::MonitorsChanged(const std::string& session_id, const std::vector<std::string>& monitor_names,
                                          const std::string& active_monitor_name) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method =
            environment.GetMethodID(listener_class, "onMonitorsChanged", "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto monitor_names_handle = MakeStringArray(environment, monitor_names);
        const auto monitor_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(active_monitor_name.c_str()));
        if (method != nullptr && session_id_handle != 0U && monitor_names_handle != 0U && monitor_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle),
                                       reinterpret_cast<jobjectArray>(monitor_names_handle), reinterpret_cast<jstring>(monitor_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, monitor_names_handle);
        DeleteLocalReference(environment, monitor_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::VirtualDisplayResult(const std::string& session_id, const std::string& request_id, const bool accepted,
                                               const std::int32_t state, const bool topology_changed, const std::int64_t topology_generation,
                                               const std::int32_t owned_display_count, const std::string& error_code,
                                               const std::string& error_message) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onVirtualDisplayResult",
                                                    "(Ljava/lang/String;Ljava/lang/String;ZIZJILjava/lang/String;Ljava/lang/String;)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto request_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(request_id.c_str()));
        const auto error_code_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(error_code.c_str()));
        const auto error_message_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(error_message.c_str()));
        if (method != nullptr && session_id_handle != 0U && request_id_handle != 0U && error_code_handle != 0U && error_message_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), reinterpret_cast<jstring>(request_id_handle),
                                       accepted, state, topology_changed, topology_generation, owned_display_count,
                                       reinterpret_cast<jstring>(error_code_handle), reinterpret_cast<jstring>(error_message_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, request_id_handle);
        DeleteLocalReference(environment, error_code_handle);
        DeleteLocalReference(environment, error_message_handle);
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

void JavaSessionCallback::Statistics(const std::string& session_id, const std::int32_t frames_per_second, const std::int32_t latency_millis,
                                     const std::int32_t bitrate_kbps) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onStatistics", "(Ljava/lang/String;III)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        if (method != nullptr && session_id_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), frames_per_second, latency_millis,
                                       bitrate_kbps);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::ClipboardText(const std::string& session_id, const std::string& text) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onClipboardText", "(Ljava/lang/String;[B)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto text_handle = reinterpret_cast<std::uintptr_t>(environment.NewByteArray(static_cast<jsize>(text.size())));
        if (method != nullptr && session_id_handle != 0U && text_handle != 0U) {
            environment.SetByteArrayRegion(reinterpret_cast<jbyteArray>(text_handle), 0, static_cast<jsize>(text.size()),
                                           reinterpret_cast<const jbyte*>(text.data())); // NOLINT(gammaray-raw-pointer-boundary)
            if (!environment.ExceptionCheck()) {
                environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), reinterpret_cast<jbyteArray>(text_handle));
            }
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, text_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::FileTransferProgress(const std::string& session_id, const px::ft::TransferJobStatus& status) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onFileTransferProgress", "(Ljava/lang/String;IIIJJJDZ)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        if (method != nullptr && session_id_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), status.id, status.file_num, status.file_count,
                                       static_cast<jlong>(status.total_size), static_cast<jlong>(status.finished_size),
                                       static_cast<jlong>(status.transferred), status.speed, status.is_remote);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::FileTransferDone(const std::string& session_id, const std::int32_t job_id, const std::string& error) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onFileTransferDone", "(Ljava/lang/String;I[B)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto error_handle = MakeByteArray(environment, error);
        if (method != nullptr && session_id_handle != 0U && error_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), job_id,
                                       reinterpret_cast<jbyteArray>(error_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, error_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::FileTransferOverwrite(const std::string& session_id, const std::int32_t job_id, const std::int32_t file_number,
                                                const std::string& path, const bool upload, const bool identical) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onFileTransferOverwrite", "(Ljava/lang/String;II[BZZ)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto path_handle = MakeByteArray(environment, path);
        if (method != nullptr && session_id_handle != 0U && path_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), job_id, file_number,
                                       reinterpret_cast<jbyteArray>(path_handle), upload, identical);
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, path_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::RecordingState(const std::string& session_id, const std::string& recording_id, const std::int32_t state,
                                         const std::string& error) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onRecordingState", "(Ljava/lang/String;Ljava/lang/String;I[B)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto recording_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(recording_id.c_str()));
        const auto error_handle = MakeByteArray(environment, error);
        if (method != nullptr && session_id_handle != 0U && recording_id_handle != 0U && error_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle),
                                       reinterpret_cast<jstring>(recording_id_handle), state, reinterpret_cast<jbyteArray>(error_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, recording_id_handle);
        DeleteLocalReference(environment, error_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::VoiceCallState(const std::string& session_id, const NativeVoiceCallStatus& status) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onVoiceCallState", "(Ljava/lang/String;IZZZ[B)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto reason_handle = MakeByteArray(environment, status.reason);
        if (method != nullptr && session_id_handle != 0U && reason_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), status.phase, status.microphone_muted,
                                       status.speaker_muted, status.requires_headset, reinterpret_cast<jbyteArray>(reason_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, reason_handle);
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
    : config_(std::move(config)), callback_(std::move(callback)), surface_(std::move(surface)), audio_player_(std::make_unique<NativeAudioPlayer>()) {
}

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
    params->enable_audio_ = config_.enable_audio;
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
            std::shared_ptr<NativeVoiceCall> voice_call;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                voice_call = self->voice_call_;
            }
            if (voice_call) {
                voice_call->Stop(false, "network_lost");
            }
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

    initialized_ = sdk_->Init(params, reinterpret_cast<void*>(surface_.get()), DecoderRenderType::kMediaCodecSurface);
    if (!initialized_)
        return false;
    statistics_ = px::SdkStatistics::Instance();
    last_received_bytes_ = statistics_->recv_data_size_.load();

    voice_call_ = NativeVoiceCall::Create(
        [weak_self](std::shared_ptr<px::Data> data) {
            const auto self = weak_self.lock();
            if (!self || !data || self->stopped_.load()) {
                return false;
            }
            std::shared_ptr<px::ThunderSdk> sdk;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                sdk = self->sdk_;
            }
            if (!sdk) {
                return false;
            }
            sdk->PostMediaMessage(std::move(data));
            return true;
        },
        [weak_self](std::function<void()> task) {
            const auto self = weak_self.lock();
            if (!self || !task || self->stopped_.load()) {
                return false;
            }
            std::shared_ptr<px::ThunderSdk> sdk;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                sdk = self->sdk_;
            }
            if (!sdk) {
                return false;
            }
            sdk->PostMiscTask(std::move(task));
            return true;
        },
        [weak_self](const NativeVoiceCallStatus& status) {
            if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                self->callback_->VoiceCallState(self->config_.session_id, status);
            }
        });
    if (!voice_call_) {
        return false;
    }

    file_transfer_session_ = px::ft::FtAsyncSession::Create(
        [weak_self](const std::shared_ptr<const px::Message>& message) {
            const auto self = weak_self.lock();
            if (!self || !message || self->stopped_.load()) {
                return px::FileTransferSendResult::Disconnected("Android session is stopping");
            }
            std::shared_ptr<px::ThunderSdk> sdk;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                sdk = self->sdk_;
            }
            if (!sdk) {
                return px::FileTransferSendResult::Disconnected("Android transport is unavailable");
            }
            const auto outgoing = std::make_shared<px::Message>(*message);
            outgoing->set_type(outgoing->has_file_response() ? px::MessageType::kFileResponse : px::MessageType::kFileAction);
            outgoing->set_device_id(self->config_.client_device_id);
            outgoing->set_stream_id(self->config_.stream_id);
            return sdk->PostFileTransferMessage(px::ProtoAsData(outgoing));
        },
        [weak_self](const std::shared_ptr<px::ft::FtEngine>& engine) {
            engine->SetLogCallback([](const std::string& message) { LOGW("[pixels_android_ft] {}", message); });
            engine->SetProgressCallback([weak_self](const px::ft::TransferJobStatus& status) {
                if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                    self->callback_->FileTransferProgress(self->config_.session_id, status);
                }
            });
            engine->SetJobDoneCallback([weak_self](const std::int32_t job_id, const std::int32_t, const std::string& error) {
                if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                    self->callback_->FileTransferDone(self->config_.session_id, job_id, error);
                }
            });
            engine->SetOverwriteConfirmCallback([weak_self](const std::int32_t job_id, const std::int32_t file_number, const std::string& path,
                                                            const bool upload, const bool identical) {
                if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                    self->callback_->FileTransferOverwrite(self->config_.session_id, job_id, file_number, path, upload, identical);
                }
            });
        });
    file_transfer_ready_ = file_transfer_session_->Start();
    if (!file_transfer_ready_) {
        file_transfer_session_.reset();
        LOGE("Pixels Android file-transfer session failed to start");
    }

    recording_thread_ = px::Thread::Make("pixels-recording", -1);
    recording_thread_->Poll();

    sdk_->SetOnEncodedVideoFrameCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load() || !message->has_video_frame()) {
            return;
        }
        const auto generation = self->active_recording_generation_.load(std::memory_order_acquire);
        std::shared_ptr<px::Thread> worker;
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            worker = self->recording_thread_;
        }
        if (generation == 0U || !worker) {
            return;
        }
        self->recording_video_packets_.fetch_add(1U, std::memory_order_relaxed);
        worker->Post([weak_self, generation, message = std::move(message)] {
            const auto owner = weak_self.lock();
            if (!owner || owner->recording_writer_generation_ != generation || !owner->recording_writer_) {
                return;
            }
            const auto& frame = message->video_frame();
            if (frame.data().empty() || frame.frame_width() <= 0 || frame.frame_height() <= 0) {
                return;
            }
            const std::vector<std::uint8_t> data(frame.data().begin(), frame.data().end());
            const auto codec = frame.type() == px::kNetHevc ? px::RecordVideoCodec::kH265 : px::RecordVideoCodec::kH264;
            owner->recording_writer_->OnEncodedVideo(data, codec, frame.frame_width(), frame.frame_height(), frame.key());
        });
    });
    sdk_->SetOnEncodedAudioFrameCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load() || !message->has_audio_frame() || message->audio_frame().data().empty()) {
            return;
        }
        const auto generation = self->active_recording_generation_.load(std::memory_order_acquire);
        std::shared_ptr<px::Thread> worker;
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            worker = self->recording_thread_;
        }
        if (generation == 0U || !worker) {
            return;
        }
        self->recording_audio_packets_.fetch_add(1U, std::memory_order_relaxed);
        worker->Post([weak_self, generation, message = std::move(message)] {
            const auto owner = weak_self.lock();
            if (!owner || owner->recording_writer_generation_ != generation || !owner->recording_writer_) {
                return;
            }
            const auto& encoded = message->audio_frame().data();
            const std::vector<std::uint8_t> data(encoded.begin(), encoded.end());
            owner->recording_writer_->OnEncodedAudio(data);
        });
    });

    sdk_->SetOnServerConfigurationCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load()) {
            return;
        }
        const auto& server_config = message->config();
        std::vector<std::string> monitor_names;
        monitor_names.reserve(static_cast<std::size_t>(server_config.monitors_info_size()));
        for (const auto& monitor : server_config.monitors_info()) {
            if (!monitor.name().empty() && std::find(monitor_names.begin(), monitor_names.end(), monitor.name()) == monitor_names.end()) {
                monitor_names.push_back(monitor.name());
            }
        }
        if (!server_config.capturing_monitor_name().empty() &&
            std::find(monitor_names.begin(), monitor_names.end(), server_config.capturing_monitor_name()) == monitor_names.end()) {
            monitor_names.push_back(server_config.capturing_monitor_name());
        }
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            self->active_monitor_name_ = server_config.capturing_monitor_name();
            self->monitor_names_ = monitor_names;
        }
        self->callback_->Connected(
            self->config_, monitor_names, server_config.capturing_monitor_name(), self->config_.enable_audio && server_config.audio_enabled(),
            self->config_.enable_input && server_config.can_be_operated(), self->file_transfer_ready_ && server_config.file_transfer_enabled(),
            self->config_.enable_clipboard && server_config.can_be_operated(), server_config.virtual_display_enabled(),
            static_cast<std::int32_t>(server_config.virtual_display_owned_count()),
            static_cast<std::int32_t>(server_config.virtual_display_max_count()), static_cast<std::int64_t>(server_config.topology_generation()),
            server_config.voice_call_enabled() && server_config.voice_call_protocol_version() == 1U,
            server_config.voice_call_requires_headset());
        std::shared_ptr<NativeVoiceCall> voice_call;
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            voice_call = self->voice_call_;
        }
        if (voice_call) {
            voice_call->SetCapabilities(server_config.voice_call_enabled() && server_config.voice_call_protocol_version() == 1U,
                                        server_config.voice_call_requires_headset());
        }
    });
    sdk_->SetOnMonitorSwitchedCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || !message->has_monitor_switched() || self->stopped_.load())
            return;
        std::vector<std::string> monitor_names;
        std::string active_monitor_name;
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            active_monitor_name = message->monitor_switched().name();
            if (active_monitor_name.empty())
                return;
            self->active_monitor_name_ = active_monitor_name;
            if (std::find(self->monitor_names_.begin(), self->monitor_names_.end(), active_monitor_name) == self->monitor_names_.end()) {
                self->monitor_names_.push_back(active_monitor_name);
            }
            monitor_names = self->monitor_names_;
        }
        self->callback_->MonitorsChanged(self->config_.session_id, monitor_names, active_monitor_name);
    });
    sdk_->SetOnRawMessageCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load()) {
            return;
        }
        if (message->type() == px::kVoiceCallRequest || message->type() == px::kVoiceCallResponse || message->type() == px::kVoiceAudioConfig ||
            message->type() == px::kVoiceAudioFrame) {
            std::shared_ptr<NativeVoiceCall> voice_call;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                voice_call = self->voice_call_;
            }
            if (voice_call) {
                voice_call->HandleMessage(message);
            }
            return;
        }
        if (message->type() == px::kFileAction || message->type() == px::kFileResponse) {
            std::shared_ptr<px::ft::FtAsyncSession> session;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                session = self->file_transfer_session_;
            }
            if (!session) {
                return;
            }
            static_cast<void>(session->Post("pixels-android-ft-inbound", [message = std::move(message)](const auto& engine) {
                if (message->type() == px::kFileAction && message->has_file_action()) {
                    engine->HandleFileAction(message->file_action(), message->stream_id());
                } else if (message->type() == px::kFileResponse && message->has_file_response()) {
                    engine->HandleFileResponse(message->file_response());
                }
            }));
            return;
        }
        if (message->type() != px::kVirtualDisplayResponse || !message->has_virtual_display_response()) {
            return;
        }
        const auto& response = message->virtual_display_response();
        self->callback_->VirtualDisplayResult(
            self->config_.session_id, response.request_id(), response.accepted(), static_cast<std::int32_t>(response.state()),
            response.topology_changed(), static_cast<std::int64_t>(response.topology_generation()),
            static_cast<std::int32_t>(response.owned_display_count()), response.error_code(), response.error_message());
    });
    sdk_->SetOnClipboardCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load() || !self->config_.enable_clipboard || message->type() != px::kClipboardInfo ||
            !message->has_clipboard_info() || message->clipboard_info().type() != px::kClipboardText || message->clipboard_info().msg().empty() ||
            message->clipboard_info().msg().size() > 1'048'576U) {
            return;
        }
        self->callback_->ClipboardText(self->config_.session_id, message->clipboard_info().msg());
    });
    sdk_->SetOnHeartBeatCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || !message->has_on_heartbeat() || self->stopped_.load())
            return;
        const auto sent_at = message->on_heartbeat().timestamp();
        const auto received_at = px::TimeUtil::GetCurrentTimestamp();
        self->latest_latency_millis_.store(received_at >= sent_at ? static_cast<std::int32_t>(received_at - sent_at) : 0);
    });
    sdk_->SetOnVideoFrameDecodedCallback([weak_self](std::shared_ptr<px::RawImage> image, const px::SdkCaptureMonitorInfo&) {
        const auto self = weak_self.lock();
        if (!self || !image || self->stopped_.load()) {
            return;
        }
        bool size_changed{};
        bool statistics_due{};
        std::int32_t frames_per_second{};
        std::int32_t bitrate_kbps{};
        {
            std::lock_guard state_lock(self->lifecycle_mutex_);
            size_changed = self->last_video_width_ != image->img_width || self->last_video_height_ != image->img_height;
            self->last_video_width_ = image->img_width;
            self->last_video_height_ = image->img_height;
            ++self->decoded_frames_in_window_;
            const auto now = std::chrono::steady_clock::now();
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - self->statistics_window_started_).count();
            if (elapsed >= 1'000) {
                frames_per_second = static_cast<std::int32_t>((self->decoded_frames_in_window_ * 1'000) / elapsed);
                const auto received_bytes = self->statistics_->recv_data_size_.load();
                const auto received_delta = received_bytes >= self->last_received_bytes_ ? received_bytes - self->last_received_bytes_ : 0;
                bitrate_kbps = static_cast<std::int32_t>((received_delta * 8) / elapsed);
                self->last_received_bytes_ = received_bytes;
                self->decoded_frames_in_window_ = 0;
                self->statistics_window_started_ = now;
                statistics_due = true;
            }
        }
        if (size_changed)
            self->callback_->FrameSizeChanged(self->config_.session_id, image->img_width, image->img_height);
        if (statistics_due) {
            self->callback_->Statistics(self->config_.session_id, frames_per_second, self->latest_latency_millis_.load(), bitrate_kbps);
        }
    });
    sdk_->SetOnAudioFrameDecodedCallback(
        [weak_self](const std::shared_ptr<px::Data>& pcm, const int sample_rate, const int channels, const int bits_per_sample) {
            if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                static_cast<void>(self->audio_player_->Write(pcm, sample_rate, channels, bits_per_sample));
            }
        });

    return true;
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
    if (!surface)
        return false;
    return QueueSurfaceUpdate(std::shared_ptr<ANativeWindow>{std::move(surface)});
}

bool NativeSession::DetachSurface() {
    std::lock_guard command_lock(command_mutex_);
    std::lock_guard state_lock(lifecycle_mutex_);
    return !stopped_.load() && sdk_ != nullptr;
}

bool NativeSession::QueueSurfaceUpdate(std::shared_ptr<ANativeWindow> surface) {
    std::shared_ptr<px::ThunderSdk> sdk;
    std::shared_ptr<ANativeWindow> retiring_surface;
    std::uintptr_t surface_handle{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !sdk_) {
            return false;
        }
        if (surface_update_in_progress_) {
            pending_surface_ = std::move(surface);
            has_pending_surface_update_ = true;
            return true;
        }
        retiring_surface = std::exchange(surface_, std::shared_ptr<ANativeWindow>{std::move(surface)});
        sdk = sdk_;
        surface_handle = surface_ ? reinterpret_cast<std::uintptr_t>(surface_.get()) : 0U; // NOLINT(gammaray-raw-pointer-boundary)
        surface_update_in_progress_ = true;
    }
    DispatchSurfaceUpdate(std::move(sdk), std::move(retiring_surface), surface_handle);
    return true;
}

void NativeSession::CompleteSurfaceUpdate() {
    std::shared_ptr<px::ThunderSdk> sdk;
    std::shared_ptr<ANativeWindow> retiring_surface;
    std::uintptr_t surface_handle{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        surface_update_in_progress_ = false;
        if (stopped_.load() || !sdk_ || !has_pending_surface_update_) {
            return;
        }
        has_pending_surface_update_ = false;
        sdk = sdk_;
        retiring_surface = std::exchange(surface_, std::move(pending_surface_));
        surface_handle = surface_ ? reinterpret_cast<std::uintptr_t>(surface_.get()) : 0U; // NOLINT(gammaray-raw-pointer-boundary)
        surface_update_in_progress_ = true;
    }
    DispatchSurfaceUpdate(std::move(sdk), std::move(retiring_surface), surface_handle);
}

void NativeSession::DispatchSurfaceUpdate(std::shared_ptr<px::ThunderSdk> sdk, std::shared_ptr<ANativeWindow> retiring_surface,
                                          const std::uintptr_t surface_handle) {
    const auto weak_self = weak_from_this();
    sdk->UpdateRenderSurface(surface_handle, [weak_self, retiring_surface = std::move(retiring_surface)]() {
        static_cast<void>(retiring_surface);
        if (const auto self = weak_self.lock())
            self->CompleteSurfaceUpdate();
    });
}

bool NativeSession::SendMouse(const std::int32_t action, const std::int32_t button, const bool down, const float x_ratio, const float y_ratio,
                              const std::int32_t delta_x, const std::int32_t delta_y) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    std::string monitor_name;
    float cursor_x{};
    float cursor_y{};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_ || action < kMouseMoveAbsolute || action > kMouseWheel) {
            return false;
        }
        if (action == kMouseMoveAbsolute) {
            if (!std::isfinite(x_ratio) || !std::isfinite(y_ratio) || x_ratio < 0.0F || x_ratio > 1.0F || y_ratio < 0.0F || y_ratio > 1.0F) {
                return false;
            }
            virtual_cursor_x_ = x_ratio;
            virtual_cursor_y_ = y_ratio;
        } else if (action == kMouseMoveRelative) {
            if (!std::isfinite(x_ratio) || !std::isfinite(y_ratio)) {
                return false;
            }
            virtual_cursor_x_ = std::clamp(virtual_cursor_x_ + x_ratio, 0.0F, 1.0F);
            virtual_cursor_y_ = std::clamp(virtual_cursor_y_ + y_ratio, 0.0F, 1.0F);
        } else if (action == kMouseButton && std::isfinite(x_ratio) && std::isfinite(y_ratio)) {
            if (x_ratio < 0.0F || x_ratio > 1.0F || y_ratio < 0.0F || y_ratio > 1.0F) {
                return false;
            }
            virtual_cursor_x_ = x_ratio;
            virtual_cursor_y_ = y_ratio;
        }
        cursor_x = virtual_cursor_x_;
        cursor_y = virtual_cursor_y_;
        sdk = sdk_;
        monitor_name = active_monitor_name_;
    }
    if (monitor_name.empty())
        return false;

    if (action == kMouseMoveAbsolute || action == kMouseMoveRelative) {
        const auto message = px::ProtoMessageMaker::MakeMouseEvent(px::ButtonFlag::kMouseMove, monitor_name, cursor_x, cursor_y, 0, false, false,
                                                                   client_signal_device_id_, config_.stream_id);
        if (!message)
            return false;
        sdk->PostMediaMessage(message);
        return true;
    }
    if (action == kMouseButton) {
        const auto flag = MouseButtonFlag(button, down);
        if (flag == px::ButtonFlag::kNone)
            return false;
        const auto message = px::ProtoMessageMaker::MakeMouseEvent(flag, monitor_name, cursor_x, cursor_y, 0, down, !down, client_signal_device_id_,
                                                                   config_.stream_id);
        if (!message)
            return false;
        sdk->PostMediaMessage(message);
        return true;
    }
    if (delta_y != 0) {
        sdk->PostMediaMessage(px::ProtoMessageMaker::MakeMouseEvent(px::ButtonFlag::kMouseEventWheel, monitor_name, cursor_x, cursor_y, delta_y,
                                                                    false, false, client_signal_device_id_, config_.stream_id));
    }
    if (delta_x != 0) {
        sdk->PostMediaMessage(px::ProtoMessageMaker::MakeMouseEvent(px::ButtonFlag::kMouseEventHWheel, monitor_name, cursor_x, cursor_y, delta_x,
                                                                    false, false, client_signal_device_id_, config_.stream_id));
    }
    if (delta_x == 0 && delta_y == 0)
        return false;
    return true;
}

bool NativeSession::SendKey(const std::int32_t virtual_key_code, const bool down) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_ || virtual_key_code <= 0 || virtual_key_code > 0xFF)
            return false;
        sdk = sdk_;
    }
    const auto message =
        px::ProtoMessageMaker::MakeKeyEvent(static_cast<std::uint32_t>(virtual_key_code), down, client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

bool NativeSession::SendGamepad(const NativeGamepadState& state) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        const auto axes_valid = state.left_thumb_x >= -32768 && state.left_thumb_x <= 32767 && state.left_thumb_y >= -32768 &&
                                state.left_thumb_y <= 32767 && state.right_thumb_x >= -32768 && state.right_thumb_x <= 32767 &&
                                state.right_thumb_y >= -32768 && state.right_thumb_y <= 32767;
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_ || state.buttons < 0 || state.buttons > 0xFFFF || state.left_trigger < 0 ||
            state.left_trigger > 0xFF || state.right_trigger < 0 || state.right_trigger > 0xFF || !axes_valid) {
            return false;
        }
        sdk = sdk_;
    }
    const auto message =
        px::ProtoMessageMaker::MakeGamepadState(state.buttons, state.left_trigger, state.right_trigger, state.left_thumb_x, state.left_thumb_y,
                                                state.right_thumb_x, state.right_thumb_y, client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

bool NativeSession::SendText(const std::string& text) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_)
            return false;
        sdk = sdk_;
    }
    const auto message = px::ProtoMessageMaker::MakeTextInput(text, client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

bool NativeSession::SendClipboardText(const std::string& text) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (!started_ || stopped_.load() || !config_.enable_clipboard || text.empty() || text.size() > 1'048'576U)
            return false;
        sdk = sdk_;
    }
    px::Message message;
    message.set_type(px::kClipboardInfo);
    message.set_device_id(config_.remote_device_id);
    message.set_stream_id(config_.stream_id);
    message.mutable_clipboard_info()->set_type(px::kClipboardText);
    message.mutable_clipboard_info()->set_msg(text);
    const auto data = px::ProtoAsData(&message);
    if (!data)
        return false;
    sdk->PostMediaMessage(data);
    return true;
}

bool NativeSession::SendSecureAttention() {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_)
            return false;
        sdk = sdk_;
    }
    const auto message = px::ProtoMessageMaker::MakeCtrlAltDelete(client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

bool NativeSession::SwitchMonitor(const std::string& monitor_name) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !sdk_ || monitor_name.empty() ||
            std::find(monitor_names_.begin(), monitor_names_.end(), monitor_name) == monitor_names_.end()) {
            return false;
        }
        if (active_monitor_name_ == monitor_name)
            return true;
        sdk = sdk_;
    }
    const auto message = px::ProtoMessageMaker::MakeChangeMonitor(0, monitor_name, client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

bool NativeSession::RequestVirtualDisplay(const std::string& request_id, const std::int32_t operation, const std::int32_t width,
                                          const std::int32_t height, const std::int32_t refresh_hz) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !config_.enable_input || !sdk_ || request_id.empty() || operation < 0 || operation > 1 || width <= 0 ||
            height <= 0 || refresh_hz <= 0) {
            return false;
        }
        sdk = sdk_;
    }
    const auto message =
        px::ProtoMessageMaker::MakeVirtualDisplayRequest(request_id, operation, static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height),
                                                         static_cast<std::uint32_t>(refresh_hz), client_signal_device_id_, config_.stream_id);
    if (!message)
        return false;
    sdk->PostMediaMessage(message);
    return true;
}

bool NativeSession::SetAudioEnabled(const bool enabled) {
    std::lock_guard command_lock(command_mutex_);
    if (stopped_.load() || !audio_player_)
        return false;
    audio_player_->SetEnabled(enabled && config_.enable_audio);
    return true;
}

std::int32_t NativeSession::StartFileUpload(const std::string& local_path, const std::string& remote_directory) {
    std::lock_guard command_lock(command_mutex_);
    if (local_path.empty() || remote_directory.empty() || local_path.size() > 4096U || remote_directory.size() > 4096U) {
        return 0;
    }
    std::shared_ptr<px::ft::FtAsyncSession> session;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !file_transfer_ready_) {
            return 0;
        }
        session = file_transfer_session_;
    }
    if (!session) {
        return 0;
    }
    const auto job_id = std::make_shared<std::atomic_int32_t>(0);
    const bool completed = session->PostAndWait(
        "pixels-android-ft-upload",
        [local_path, remote_directory, stream_id = config_.stream_id, job_id](const auto& engine) {
            job_id->store(engine->SendFiles(local_path, false, remote_directory, 0, false, stream_id), std::memory_order_release);
        },
        std::chrono::seconds(2));
    return completed ? job_id->load(std::memory_order_acquire) : 0;
}

std::int32_t NativeSession::StartFileDownload(const std::string& remote_path, const std::string& local_directory) {
    std::lock_guard command_lock(command_mutex_);
    if (remote_path.empty() || local_directory.empty() || remote_path.size() > 4096U || local_directory.size() > 4096U) {
        return 0;
    }
    std::shared_ptr<px::ft::FtAsyncSession> session;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !file_transfer_ready_) {
            return 0;
        }
        session = file_transfer_session_;
    }
    if (!session) {
        return 0;
    }
    const auto job_id = std::make_shared<std::atomic_int32_t>(0);
    const bool completed = session->PostAndWait(
        "pixels-android-ft-download",
        [remote_path, local_directory, stream_id = config_.stream_id, job_id](const auto& engine) {
            job_id->store(engine->ReceiveFiles(remote_path, false, local_directory, 0, false, stream_id), std::memory_order_release);
        },
        std::chrono::seconds(2));
    return completed ? job_id->load(std::memory_order_acquire) : 0;
}

bool NativeSession::CancelFileTransfer(const std::int32_t job_id) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ft::FtAsyncSession> session;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || job_id <= 0) {
            return false;
        }
        session = file_transfer_session_;
    }
    return session && session->Post("pixels-android-ft-cancel", [job_id](const auto& engine) { engine->CancelJob(job_id); });
}

bool NativeSession::ConfirmFileOverwrite(const std::int32_t job_id, const std::int32_t file_number, const bool overwrite,
                                         const std::uint64_t offset_bytes, const bool apply_to_all) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ft::FtAsyncSession> session;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || job_id <= 0 || file_number < 0) {
            return false;
        }
        session = file_transfer_session_;
    }
    return session && session->Post("pixels-android-ft-confirm", [job_id, file_number, overwrite, offset_bytes, apply_to_all](const auto& engine) {
        if (apply_to_all) {
            engine->SetOverwriteStrategy(job_id, overwrite);
        }
        engine->ConfirmFile(job_id, file_number, overwrite, offset_bytes);
    });
}

bool NativeSession::StartRecording(const std::string& recording_id, const std::string& staging_directory) {
    std::lock_guard command_lock(command_mutex_);
    if (recording_id.empty() || staging_directory.empty() || recording_id.size() > 128U || staging_directory.size() > 4096U ||
        active_recording_generation_.load(std::memory_order_acquire) != 0U) {
        return false;
    }
    std::shared_ptr<px::Thread> worker;
    std::shared_ptr<px::ThunderSdk> sdk;
    std::string monitor_name;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !recording_thread_ || !sdk_) {
            return false;
        }
        worker = recording_thread_;
        sdk = sdk_;
        monitor_name = active_monitor_name_;
    }
    const auto generation = ++next_recording_generation_;
    recording_video_packets_.store(0U, std::memory_order_relaxed);
    recording_audio_packets_.store(0U, std::memory_order_relaxed);
    active_recording_id_ = recording_id;
    active_recording_generation_.store(generation, std::memory_order_release);
    const auto weak_self = weak_from_this();
    worker->Post([weak_self, weak_sdk = std::weak_ptr<px::ThunderSdk>{sdk}, generation, recording_id, staging_directory, monitor_name] {
        const auto self = weak_self.lock();
        if (!self || self->active_recording_generation_.load(std::memory_order_acquire) != generation) {
            return;
        }
        try {
            self->recording_writer_ = px::RecordWriter::Make(px::RecordWriterConfig{
                .dir = staging_directory,
                .monitor_name = monitor_name,
                .file_prefix = "pixels_",
                .max_segment_bytes = 8LL * 1024 * 1024 * 1024,
                .max_file_count = 0,
                .on_request_keyframe = [weak_sdk] {
                    if (const auto active_sdk = weak_sdk.lock()) {
                        active_sdk->RequestVideoKeyFrame();
                    }
                },
            });
            self->recording_writer_generation_ = generation;
            self->callback_->RecordingState(self->config_.session_id, recording_id, kRecordingStarted, {});
            if (const auto active_sdk = weak_sdk.lock()) {
                active_sdk->RequestVideoKeyFrame();
            }
        } catch (const std::exception& error) {
            std::uint64_t expected = generation;
            self->active_recording_generation_.compare_exchange_strong(expected, 0U, std::memory_order_acq_rel);
            self->recording_writer_.reset();
            self->recording_writer_generation_ = 0U;
            self->callback_->RecordingState(self->config_.session_id, recording_id, kRecordingFailed, error.what());
        }
    });
    return true;
}

bool NativeSession::StopRecording(const std::string& recording_id) {
    std::lock_guard command_lock(command_mutex_);
    if (recording_id.empty() || recording_id != active_recording_id_) {
        return false;
    }
    const auto generation = active_recording_generation_.exchange(0U, std::memory_order_acq_rel);
    active_recording_id_.clear();
    std::shared_ptr<px::Thread> worker;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        worker = recording_thread_;
    }
    if (generation == 0U || !worker) {
        return false;
    }
    const auto weak_self = weak_from_this();
    worker->Post([weak_self, generation, recording_id] {
        const auto self = weak_self.lock();
        if (!self) {
            return;
        }
        std::string error;
        if (self->recording_writer_generation_ == generation && self->recording_writer_) {
            try {
                self->recording_writer_->Stop();
            } catch (const std::exception& exception) {
                error = exception.what();
            }
            self->recording_writer_.reset();
            self->recording_writer_generation_ = 0U;
        }
        LOGI("Pixels Android recording {} finalized with {} video and {} audio packets", recording_id,
             self->recording_video_packets_.load(std::memory_order_relaxed), self->recording_audio_packets_.load(std::memory_order_relaxed));
        self->callback_->RecordingState(self->config_.session_id, recording_id,
                                        error.empty() ? kRecordingCompleted : kRecordingFailed, error);
    });
    return true;
}

bool NativeSession::StartVoiceCall() {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<NativeVoiceCall> voice_call;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !voice_call_) {
            return false;
        }
        voice_call = voice_call_;
    }
    return voice_call->Start(client_signal_device_id_, config_.stream_id);
}

bool NativeSession::StopVoiceCall() {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<NativeVoiceCall> voice_call;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !voice_call_) {
            return false;
        }
        voice_call = voice_call_;
    }
    voice_call->Stop(true, "local_hangup");
    return true;
}

bool NativeSession::SetVoiceMicrophoneMuted(const bool muted) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<NativeVoiceCall> voice_call;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !voice_call_) {
            return false;
        }
        voice_call = voice_call_;
    }
    return voice_call->SetMicrophoneMuted(muted);
}

bool NativeSession::SetVoiceSpeakerMuted(const bool muted) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<NativeVoiceCall> voice_call;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !voice_call_) {
            return false;
        }
        voice_call = voice_call_;
    }
    return voice_call->SetSpeakerMuted(muted);
}

void NativeSession::Stop() {
    std::lock_guard command_lock(command_mutex_);
    if (stopped_.exchange(true)) {
        return;
    }
    std::shared_ptr<px::ThunderSdk> sdk;
    std::shared_ptr<px::ft::FtAsyncSession> file_transfer_session;
    std::shared_ptr<px::Thread> recording_thread;
    std::shared_ptr<NativeVoiceCall> voice_call;
    std::shared_ptr<ANativeWindow> surface;
    const auto recording_generation = active_recording_generation_.exchange(0U, std::memory_order_acq_rel);
    const auto recording_id = std::exchange(active_recording_id_, {});
    {
        std::lock_guard lock(lifecycle_mutex_);
        sdk = std::move(sdk_);
        file_transfer_session = std::move(file_transfer_session_);
        recording_thread = std::move(recording_thread_);
        voice_call = std::move(voice_call_);
        file_transfer_ready_ = false;
        session_listener_.reset();
        message_notifier_.reset();
        surface = std::move(surface_);
        pending_surface_.reset();
        has_pending_surface_update_ = false;
    }
    if (file_transfer_session) {
        static_cast<void>(file_transfer_session->PostAndWait(
            "pixels-android-ft-cancel-before-stop",
            [](const auto& engine) {
                std::vector<std::int32_t> job_ids;
                job_ids.reserve(engine->read_jobs().size() + engine->write_jobs().size());
                for (const auto& job : engine->read_jobs()) {
                    job_ids.push_back(job.id());
                }
                for (const auto& job : engine->write_jobs()) {
                    job_ids.push_back(job.id());
                }
                for (const auto job_id : job_ids) {
                    engine->CancelJob(job_id);
                }
            },
            std::chrono::seconds(2)));
        static_cast<void>(file_transfer_session->StopAndWait(std::chrono::seconds(2)));
    }
    if (recording_thread) {
        const auto finalized = std::make_shared<std::promise<void>>();
        const auto completion = finalized->get_future();
        const auto weak_self = weak_from_this();
        recording_thread->Post([weak_self, recording_generation, recording_id, finalized] {
            if (const auto self = weak_self.lock()) {
                std::string error;
                if (self->recording_writer_generation_ == recording_generation && self->recording_writer_) {
                    try {
                        self->recording_writer_->Stop();
                    } catch (const std::exception& exception) {
                        error = exception.what();
                    }
                    self->recording_writer_.reset();
                    self->recording_writer_generation_ = 0U;
                }
                if (recording_generation != 0U && !recording_id.empty()) {
                    self->callback_->RecordingState(self->config_.session_id, recording_id,
                                                    error.empty() ? kRecordingCompleted : kRecordingFailed, error);
                }
            }
            finalized->set_value();
        });
        static_cast<void>(completion.wait_for(std::chrono::seconds(5)));
        recording_thread->Exit();
    }
    if (voice_call) {
        // The SDK transport is about to close and stopped_ already rejects new
        // asynchronous sends. Tear the media endpoint down deterministically;
        // the peer observes the session transport closing.
        voice_call->Stop(false, "session_ended");
    }
    if (sdk)
        sdk->Exit();
    audio_player_->Stop();
}

} // namespace pixels::android
