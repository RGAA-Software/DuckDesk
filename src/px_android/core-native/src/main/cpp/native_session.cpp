#include "native_session.h"

#include "native_audio_player.h"
#include "native_clipboard.h"
#include "px_client_sdk/sdk_voice_call.h"
#include "px_client_sdk/platform/voice_audio_endpoint_port.h"
#include "data.h"

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
#include "px_client_sdk/platform/android/android_decoder_factory.h"
#include "px_client_sdk/platform/android/android_video_output.h"
#include "px_client_sdk/sdk_params.h"
#include "px_client_sdk/sdk_statistics.h"
#include "px_client_sdk/sdk_messages.h"
#include "px_client_sdk/thunder_sdk.h"
#include "px_common/md5.h"
#include "px_common/log.h"
#include "px_common/message_notifier.h"
#include "px_common/thread.h"
#include "px_common/time_util.h"
#include "px_client_sdk/sdk_recording_session.h"
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
enum class JavaVoicePhase : jint { kIdle = 0, kRequesting = 1, kConnected = 2 };
constexpr std::size_t kMaximumRemoteDirectoryEntries = 2048U;

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

std::uintptr_t MakeLongArray(JNIEnv& environment, const std::vector<std::int64_t>& values) {
    const auto result_handle = reinterpret_cast<std::uintptr_t>(environment.NewLongArray(static_cast<jsize>(values.size())));
    if (result_handle == 0U || values.empty()) {
        return result_handle;
    }
    environment.SetLongArrayRegion(reinterpret_cast<jlongArray>(result_handle), 0, static_cast<jsize>(values.size()),
                                   reinterpret_cast<const jlong*>(values.data())); // NOLINT(gammaray-raw-pointer-boundary)
    return environment.ExceptionCheck() ? 0U : result_handle;
}

std::uintptr_t MakeIntArray(JNIEnv& environment, const std::vector<std::int32_t>& values) {
    const auto result_handle = reinterpret_cast<std::uintptr_t>(environment.NewIntArray(static_cast<jsize>(values.size())));
    if (result_handle == 0U || values.empty()) {
        return result_handle;
    }
    environment.SetIntArrayRegion(reinterpret_cast<jintArray>(result_handle), 0, static_cast<jsize>(values.size()),
                                  reinterpret_cast<const jint*>(values.data())); // NOLINT(gammaray-raw-pointer-boundary)
    return environment.ExceptionCheck() ? 0U : result_handle;
}

std::uintptr_t MakeByteArrayArray(JNIEnv& environment, const std::vector<std::string>& values) {
    const auto byte_array_class_handle = reinterpret_cast<std::uintptr_t>(environment.FindClass("[B"));
    if (byte_array_class_handle == 0U) {
        return 0U;
    }
    const auto result_handle = reinterpret_cast<std::uintptr_t>(
        environment.NewObjectArray(static_cast<jsize>(values.size()), reinterpret_cast<jclass>(byte_array_class_handle), nullptr));
    for (std::size_t index = 0; result_handle != 0U && index < values.size(); ++index) {
        const auto value_handle = MakeByteArray(environment, values[index]);
        if (value_handle != 0U) {
            environment.SetObjectArrayElement(reinterpret_cast<jobjectArray>(result_handle), static_cast<jsize>(index),
                                              reinterpret_cast<jbyteArray>(value_handle));
            DeleteLocalReference(environment, value_handle);
        }
    }
    DeleteLocalReference(environment, byte_array_class_handle);
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
                                    const bool supports_file_transfer, const bool supports_clipboard, const bool supports_voice_call,
                                    const bool voice_call_requires_headset) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method =
            environment.GetMethodID(listener_class, "onConnected", "(Ljava/lang/String;[Ljava/lang/String;Ljava/lang/String;ZZZZZZ)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(config.session_id.c_str()));
        const auto monitor_names_handle = MakeStringArray(environment, monitor_names);
        const auto monitor_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(active_monitor_name.c_str()));
        if (method != nullptr && session_id_handle != 0U && monitor_names_handle != 0U && monitor_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle),
                                       reinterpret_cast<jobjectArray>(monitor_names_handle), reinterpret_cast<jstring>(monitor_handle),
                                       supports_audio, supports_input, supports_file_transfer, supports_clipboard, supports_voice_call,
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
                                     const std::int32_t bitrate_kbps, const std::string& decoder_name) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onStatistics", "(Ljava/lang/String;IIILjava/lang/String;)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto decoder_name_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(decoder_name.c_str()));
        if (method != nullptr && session_id_handle != 0U && decoder_name_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), frames_per_second, latency_millis,
                                       bitrate_kbps, reinterpret_cast<jstring>(decoder_name_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, decoder_name_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::GamepadRumble(
    const std::string& session_id,
    const std::int32_t strong_motor,
    const std::int32_t weak_motor) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onGamepadRumble", "(Ljava/lang/String;II)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        if (method != nullptr && session_id_handle != 0U) {
            environment.CallVoidMethod(
                listener, method, reinterpret_cast<jstring>(session_id_handle), strong_motor, weak_motor);
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

void JavaSessionCallback::ClipboardFiles(const std::string& session_id, const NativeClipboardFiles& files) const {
    std::vector<std::string> names;
    std::vector<std::int64_t> sizes;
    names.reserve(files.files.size());
    sizes.reserve(files.files.size());
    for (const auto& file : files.files) {
        names.push_back(file.display_name);
        sizes.push_back(file.size);
    }
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method =
            environment.GetMethodID(listener_class, "onClipboardFiles", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[J)V");
        const auto session_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto generation_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(files.generation.c_str()));
        const auto names_handle = MakeStringArray(environment, names);
        const auto sizes_handle = MakeLongArray(environment, sizes);
        if (method != nullptr && session_handle != 0U && generation_handle != 0U && names_handle != 0U && sizes_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_handle), reinterpret_cast<jstring>(generation_handle),
                                       reinterpret_cast<jobjectArray>(names_handle), reinterpret_cast<jlongArray>(sizes_handle));
        }
        DeleteLocalReference(environment, session_handle);
        DeleteLocalReference(environment, generation_handle);
        DeleteLocalReference(environment, names_handle);
        DeleteLocalReference(environment, sizes_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::ClipboardFilesReady(const std::string& session_id, const std::string& generation,
                                              const std::vector<std::string>& paths, const std::string& error) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method =
            environment.GetMethodID(listener_class, "onClipboardFilesReady", "(Ljava/lang/String;Ljava/lang/String;[Ljava/lang/String;[B)V");
        const auto session_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto generation_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(generation.c_str()));
        const auto paths_handle = MakeStringArray(environment, paths);
        const auto error_handle = MakeByteArray(environment, error);
        if (method != nullptr && session_handle != 0U && generation_handle != 0U && paths_handle != 0U && error_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_handle), reinterpret_cast<jstring>(generation_handle),
                                       reinterpret_cast<jobjectArray>(paths_handle), reinterpret_cast<jbyteArray>(error_handle));
        }
        DeleteLocalReference(environment, session_handle);
        DeleteLocalReference(environment, generation_handle);
        DeleteLocalReference(environment, paths_handle);
        DeleteLocalReference(environment, error_handle);
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

void JavaSessionCallback::RemoteDirectory(const std::string& session_id, const px::FileDirectory& directory) const {
    const auto count = std::min(static_cast<std::size_t>(directory.entries_size()), kMaximumRemoteDirectoryEntries);
    std::vector<std::string> names;
    std::vector<std::string> absolute_paths;
    std::vector<std::int32_t> types;
    std::vector<std::int64_t> sizes;
    std::vector<std::int64_t> modified_times;
    names.reserve(count);
    absolute_paths.reserve(count);
    types.reserve(count);
    sizes.reserve(count);
    modified_times.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        const auto& entry = directory.entries(static_cast<int>(index));
        names.push_back(entry.name());
        absolute_paths.push_back(entry.abs_path());
        types.push_back(static_cast<std::int32_t>(entry.entry_type()));
        sizes.push_back(static_cast<std::int64_t>(entry.size()));
        modified_times.push_back(static_cast<std::int64_t>(entry.modified_time()));
    }
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onRemoteDirectory", "(Ljava/lang/String;[B[[B[I[[B[J[JZ)V");
        const auto session_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto path_handle = MakeByteArray(environment, directory.path());
        const auto names_handle = MakeByteArrayArray(environment, names);
        const auto types_handle = MakeIntArray(environment, types);
        const auto paths_handle = MakeByteArrayArray(environment, absolute_paths);
        const auto sizes_handle = MakeLongArray(environment, sizes);
        const auto times_handle = MakeLongArray(environment, modified_times);
        if (method != nullptr && session_handle != 0U && path_handle != 0U && names_handle != 0U && types_handle != 0U &&
            paths_handle != 0U && sizes_handle != 0U && times_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_handle), reinterpret_cast<jbyteArray>(path_handle),
                                       reinterpret_cast<jobjectArray>(names_handle), reinterpret_cast<jintArray>(types_handle),
                                       reinterpret_cast<jobjectArray>(paths_handle), reinterpret_cast<jlongArray>(sizes_handle),
                                       reinterpret_cast<jlongArray>(times_handle), directory.entries_size() > static_cast<int>(count));
        }
        DeleteLocalReference(environment, session_handle);
        DeleteLocalReference(environment, path_handle);
        DeleteLocalReference(environment, names_handle);
        DeleteLocalReference(environment, types_handle);
        DeleteLocalReference(environment, paths_handle);
        DeleteLocalReference(environment, sizes_handle);
        DeleteLocalReference(environment, times_handle);
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

void JavaSessionCallback::VoiceCallState(const std::string& session_id, const px::VoiceCallStatus& status) const {
    // The Java UI contract uses 0/1/2; the shared state also has an incoming-pending phase.
    const auto phase = status.phase == px::VoiceCallPhase::kConnected ? JavaVoicePhase::kConnected
                       : status.phase == px::VoiceCallPhase::kIdle    ? JavaVoicePhase::kIdle
                                                                      : JavaVoicePhase::kRequesting;
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [&](JNIEnv& environment) {
        const auto listener = reinterpret_cast<jobject>(listener_handle);
        const auto listener_class_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectClass(listener));
        const auto listener_class = reinterpret_cast<jclass>(listener_class_handle);
        const auto method = environment.GetMethodID(listener_class, "onVoiceCallState", "(Ljava/lang/String;IZZZ[B)V");
        const auto session_id_handle = reinterpret_cast<std::uintptr_t>(environment.NewStringUTF(session_id.c_str()));
        const auto reason_handle = MakeByteArray(environment, status.reason);
        if (method != nullptr && session_id_handle != 0U && reason_handle != 0U) {
            environment.CallVoidMethod(listener, method, reinterpret_cast<jstring>(session_id_handle), static_cast<jint>(phase),
                                       status.microphone_muted, status.speaker_muted, status.requires_headset,
                                       reinterpret_cast<jbyteArray>(reason_handle));
        }
        DeleteLocalReference(environment, session_id_handle);
        DeleteLocalReference(environment, reason_handle);
        DeleteLocalReference(environment, listener_class_handle);
    });
}

void JavaSessionCallback::MediaUnavailable(const std::string& session_id, const bool interrupted) const {
    const auto listener_handle = listener_handle_;
    WithEnvironment(vm_handle_, [listener_handle, session_id, interrupted](JNIEnv& environment) {
        // JNI local references are owned only within this synchronous environment scope.
        const auto delete_local = [&environment](auto reference) { environment.DeleteLocalRef(reference); };
        const std::unique_ptr<_jclass, decltype(delete_local)> listener_class{environment.GetObjectClass(reinterpret_cast<jobject>(listener_handle)),
                                                                              delete_local};
        const std::unique_ptr<_jstring, decltype(delete_local)> session_value{environment.NewStringUTF(session_id.c_str()), delete_local};
        if (!listener_class || !session_value || environment.ExceptionCheck())
            return;
        if (environment.GetMethodID(listener_class.get(), "onMediaUnavailable", "(Ljava/lang/String;Z)V") == nullptr)
            return;
        environment.CallVoidMethod(reinterpret_cast<jobject>(listener_handle),
                                   environment.GetMethodID(listener_class.get(), "onMediaUnavailable", "(Ljava/lang/String;Z)V"), session_value.get(),
                                   interrupted);
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
    params->connection_ticket_ = config_.connection_ticket;
    params->connection_nonce_ = config_.connection_nonce;
    params->connection_instance_id_ = config_.connection_instance_id;
    params->render_type_name_ = "mediacodec_surface";

    const auto weak_self = weak_from_this();
    session_listener_->Listen<px::SdkMsgNetworkDisConnected>([weak_self](const auto&) {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            std::shared_ptr<px::VoiceCallController> voice_call{};
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
    session_listener_->Listen<px::SdkMsgUdpMediaUnavailable>([weak_self](const px::SdkMsgUdpMediaUnavailable& event) {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            std::shared_ptr<px::VoiceCallController> voice_call{};
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                voice_call = self->voice_call_;
            }
            if (voice_call) {
                voice_call->SetTransportAvailable(false);
            }
            self->callback_->MediaUnavailable(self->config_.session_id, event.reason == px::UdpMediaFailure::kInterrupted);
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

    decoder_output_ = std::make_shared<px::AndroidVideoOutput>(surface_);
    initialized_ = sdk_->Init(params, px::MakeAndroidVideoDecoderFactory(decoder_output_, config_.prefer_software_decoder));
    if (!initialized_)
        return false;
    statistics_ = px::SdkStatistics::Instance();
    last_received_bytes_ = statistics_->recv_data_size_.load();

    const px::VoiceCallDependencies::MessageSender send_voice = [weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load()) {
            return false;
        }
        std::shared_ptr<px::ThunderSdk> sdk{};
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            sdk = self->sdk_;
        }
        const auto data = px::ProtoAsData(message);
        if (!sdk || !data) {
            return false;
        }
        sdk->PostMediaMessage(data);
        return true;
    };
    px::VoiceCallDependencies voice_dependencies{
        .send_control = send_voice,
        .send_audio =
            [weak_self](std::shared_ptr<px::Message> message) {
                const auto self = weak_self.lock();
                if (!self || self->stopped_.load()) {
                    return false;
                }
                std::shared_ptr<px::ThunderSdk> sdk{};
                {
                    std::lock_guard lock(self->lifecycle_mutex_);
                    sdk = self->sdk_;
                }
                return sdk && sdk->PostVoiceAudioMessage(message);
            },
        .create_audio = [] { return std::make_shared<px::VoiceAudioEndpointPort>(); },
        .post_task =
            [weak_self](std::function<void()> task) {
                const auto self = weak_self.lock();
                if (!self || !task || self->stopped_.load()) {
                    return false;
                }
                std::shared_ptr<px::ThunderSdk> sdk{};
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
        .status_changed =
            [weak_self](const px::VoiceCallStatus& status) {
                if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                    self->callback_->VoiceCallState(self->config_.session_id, status);
                }
            },
    };
    voice_call_ = px::VoiceCallController::Create({client_signal_device_id_, config_.stream_id}, std::move(voice_dependencies));
    if (!voice_call_) {
        return false;
    }

    clipboard_ = NativeClipboard::Create(
        client_signal_device_id_, config_.stream_id,
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
            return sdk && sdk->PostFileTransferMessage(std::move(data)).accepted();
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
        [weak_self](const NativeClipboardFiles& files) {
            if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                self->callback_->ClipboardFiles(self->config_.session_id, files);
            }
        },
        [weak_self](const std::string& generation, const std::vector<std::string>& paths, const std::string& error) {
            if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
                self->callback_->ClipboardFilesReady(self->config_.session_id, generation, paths, error);
            }
        });
    if (!clipboard_) {
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
            engine->SetResponseCallback([weak_self](const px::FileResponse& response) {
                if (const auto self = weak_self.lock(); self && !self->stopped_.load() && response.has_dir()) {
                    self->callback_->RemoteDirectory(self->config_.session_id, response.dir());
                }
            });
        });
    file_transfer_ready_ = file_transfer_session_->Start();
    if (!file_transfer_ready_) {
        file_transfer_session_.reset();
        LOGE("Pixels Android file-transfer session failed to start");
    }

    const auto record_encoded = [weak_self](std::shared_ptr<px::Message> message) {
        if (const auto self = weak_self.lock()) {
            self->SubmitRecordingFrame(std::move(message));
        }
    };
    sdk_->SetOnEncodedVideoFrameCallback(record_encoded);
    sdk_->SetOnEncodedAudioFrameCallback(record_encoded);
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
            self->config_.enable_clipboard && server_config.can_be_operated(),
            server_config.voice_call_enabled() && server_config.voice_call_protocol_version() == 1U, server_config.voice_call_requires_headset());
        std::shared_ptr<px::VoiceCallController> voice_call{};
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
            std::shared_ptr<px::VoiceCallController> voice_call{};
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                voice_call = self->voice_call_;
            }
            if (voice_call) {
                voice_call->HandleMessage(message);
            }
            return;
        }
        if (message->type() == px::kGamepadRumble && message->has_gamepad_rumble()) {
            const auto& rumble = message->gamepad_rumble();
            self->callback_->GamepadRumble(
                self->config_.session_id,
                static_cast<std::int32_t>(std::min(rumble.strong_motor(), 255U)),
                static_cast<std::int32_t>(std::min(rumble.weak_motor(), 255U)));
            return;
        }
        if (message->type() == px::kClipboardReqAtBegin || message->type() == px::kClipboardReqBuffer ||
            message->type() == px::kClipboardReqAtEnd || message->type() == px::kClipboardRespBuffer) {
            std::shared_ptr<NativeClipboard> clipboard;
            {
                std::lock_guard lock(self->lifecycle_mutex_);
                clipboard = self->clipboard_;
            }
            if (clipboard) {
                clipboard->HandleFileMessage(message);
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
    });
    sdk_->SetOnClipboardCallback([weak_self](std::shared_ptr<px::Message> message) {
        const auto self = weak_self.lock();
        if (!self || !message || self->stopped_.load() || !self->config_.enable_clipboard || message->type() != px::kClipboardInfo ||
            !message->has_clipboard_info()) {
            return;
        }
        std::shared_ptr<NativeClipboard> clipboard{};
        {
            std::lock_guard lock(self->lifecycle_mutex_);
            clipboard = self->clipboard_;
        }
        if (clipboard) {
            clipboard->AcceptRemoteFiles(message);
        }
        const auto& clipboard_info = message->clipboard_info();
        if (clipboard_info.type() == px::kClipboardText && !clipboard_info.msg().empty() && clipboard_info.msg().size() <= 1'048'576U) {
            self->callback_->ClipboardText(self->config_.session_id, clipboard_info.msg());
        }
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
            self->callback_->Statistics(self->config_.session_id, frames_per_second, self->latest_latency_millis_.load(), bitrate_kbps,
                                        self->statistics_->video_decoder_.Clone());
        }
    });
    sdk_->SetOnVideoDecoderFailureCallback([weak_self] {
        if (const auto self = weak_self.lock(); self && !self->stopped_.load()) {
            self->callback_->Disconnected(self->config_.session_id, 4, false);
        }
    });
    sdk_->SetOnAudioFrameDecodedCallback(
        [weak_self](const std::shared_ptr<px::Data>& pcm, const int sample_rate, const int channels, const int bits_per_sample) {
            const auto self = weak_self.lock();
            if (self && !self->stopped_.load() && pcm) {
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
    return QueueSurfaceUpdate({});
}

bool NativeSession::QueueSurfaceUpdate(std::shared_ptr<ANativeWindow> surface) {
    std::shared_ptr<px::ThunderSdk> sdk{};
    std::shared_ptr<ANativeWindow> retiring_surface{};
    std::shared_ptr<ANativeWindow> replacement{};
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
        replacement = surface_;
        surface_update_in_progress_ = true;
    }
    DispatchSurfaceUpdate(std::move(sdk), std::move(retiring_surface), std::move(replacement));
    return true;
}

void NativeSession::CompleteSurfaceUpdate() {
    std::shared_ptr<px::ThunderSdk> sdk{};
    std::shared_ptr<ANativeWindow> retiring_surface{};
    std::shared_ptr<ANativeWindow> replacement{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        surface_update_in_progress_ = false;
        if (stopped_.load() || !sdk_ || !has_pending_surface_update_) {
            return;
        }
        has_pending_surface_update_ = false;
        sdk = sdk_;
        retiring_surface = std::exchange(surface_, std::move(pending_surface_));
        replacement = surface_;
        surface_update_in_progress_ = true;
    }
    DispatchSurfaceUpdate(std::move(sdk), std::move(retiring_surface), std::move(replacement));
}

void NativeSession::DispatchSurfaceUpdate(std::shared_ptr<px::ThunderSdk> sdk, std::shared_ptr<ANativeWindow> retiring_surface,
                                          std::shared_ptr<ANativeWindow> replacement) {
    const auto output_available = replacement != nullptr;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_.load() || !decoder_output_) return;
    }
    const auto weak_self = weak_from_this();
    sdk->RefreshVideoOutput(
        output_available,
        [weak_self, retiring_surface = std::move(retiring_surface)]() {
            static_cast<void>(retiring_surface);
            if (const auto self = weak_self.lock()) self->CompleteSurfaceUpdate();
        },
        [output = decoder_output_, replacement = std::move(replacement)]() mutable { output->Replace(std::move(replacement)); });
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
    std::shared_ptr<px::ThunderSdk> sdk{};
    std::shared_ptr<NativeClipboard> clipboard{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (!started_ || stopped_.load() || !config_.enable_clipboard || text.empty() || text.size() > 1'048'576U)
            return false;
        sdk = sdk_;
        clipboard = clipboard_;
    }
    if (clipboard) {
        clipboard->RevokeLocalFiles();
    }
    px::Message message{};
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

bool NativeSession::SendClipboardFiles(const std::string& generation, std::vector<NativeClipboardFile> files) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<NativeClipboard> clipboard;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (!started_ || stopped_.load() || !config_.enable_clipboard || !clipboard_) {
            return false;
        }
        clipboard = clipboard_;
    }
    return clipboard->PublishLocalFiles(generation, std::move(files));
}

bool NativeSession::DownloadClipboardFiles(const std::string& generation, const std::string& destination_directory) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<NativeClipboard> clipboard;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (!started_ || stopped_.load() || !config_.enable_clipboard || !clipboard_) {
            return false;
        }
        clipboard = clipboard_;
    }
    return clipboard->DownloadRemoteFiles(generation, destination_directory);
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

bool NativeSession::SetFrameRate(const std::int32_t frame_rate) {
    if (frame_rate < 15 || frame_rate > 120)
        return false;
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::ThunderSdk> sdk;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !sdk_)
            return false;
        sdk = sdk_;
    }
    const auto message = std::make_shared<px::Message>();
    message->set_type(px::kModifyFps);
    message->set_device_id(client_signal_device_id_);
    message->set_stream_id(config_.stream_id);
    message->mutable_modify_fps()->set_fps(frame_rate);
    const auto data = px::ProtoAsData(message);
    if (!data)
        return false;
    sdk->PostMediaMessage(data);
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

bool NativeSession::ListRemoteDirectory(const std::string& remote_path) {
    std::lock_guard command_lock(command_mutex_);
    if (remote_path.empty() || remote_path.size() > 4096U) {
        return false;
    }
    std::shared_ptr<px::ft::FtAsyncSession> session;
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !file_transfer_ready_) {
            return false;
        }
        session = file_transfer_session_;
    }
    return session && session->Post("pixels-android-ft-list-directory",
                                    [remote_path](const auto& engine) { engine->ReadDir(remote_path, false); });
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

void NativeSession::SubmitRecordingFrame(std::shared_ptr<px::Message> message) {
    std::shared_ptr<px::RecordingSession> recording{};
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_.load())
            return;
        recording = recording_session_;
    }
    if (recording)
        static_cast<void>(recording->Submit(std::move(message)));
}

bool NativeSession::StartRecording(const std::string& recording_id, const std::string& staging_directory) {
    std::lock_guard command_lock(command_mutex_);
    if (recording_id.empty() || staging_directory.empty() || recording_id.size() > 128U || staging_directory.size() > 4096U)
        return false;
    std::shared_ptr<px::ThunderSdk> sdk{};
    std::shared_ptr<px::RecordingSession> recording{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !sdk_)
            return false;
        std::erase_if(finishing_recordings_, [](const auto& run) { return run->WaitFor(std::chrono::milliseconds::zero()); });
        if (recording_session_ && recording_session_->WaitFor(std::chrono::milliseconds::zero()))
            recording_session_.reset();
        if (recording_session_ || finishing_recordings_.size() >= 4U)
            return false;
        sdk = sdk_;
        const auto weak_callback = std::weak_ptr<JavaSessionCallback>(callback_);
        const auto session_id = config_.session_id;
        recording = px::RecordingSession::Create(
            {.writer = {
                 .dir = staging_directory,
                 .monitor_name = active_monitor_name_,
                 .file_prefix = "pixels_",
                 .max_segment_bytes = 8LL * 1024 * 1024 * 1024,
                 .max_file_count = 0,
                 .on_request_keyframe = [weak_sdk = std::weak_ptr<px::ThunderSdk>(sdk)] {
                     if (const auto active = weak_sdk.lock())
                         active->RequestVideoKeyFrame();
                 },
             }},
            {.started = [weak_callback, session_id, recording_id] {
                 if (const auto callback = weak_callback.lock())
                     callback->RecordingState(session_id, recording_id, kRecordingStarted, {});
             },
             .finished = [weak_callback, session_id, recording_id](const px::RecordingSessionResult& result) {
                 LOGI("Pixels Android recording {} finalized with {} video and {} audio packets", recording_id,
                      result.video_packets, result.audio_packets);
                 if (const auto callback = weak_callback.lock())
                     callback->RecordingState(session_id, recording_id, result.error.empty() ? kRecordingCompleted : kRecordingFailed, result.error);
             }});
        if (!recording)
            return false;
        recording_session_ = recording;
        active_recording_id_ = recording_id;
        if (!recording->Start()) {
            recording_session_.reset();
            active_recording_id_.clear();
            return false;
        }
    }
    sdk->RequestVideoKeyFrame();
    return true;
}

bool NativeSession::StopRecording(const std::string& recording_id) {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::RecordingSession> recording{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (recording_id.empty() || recording_id != active_recording_id_ || !recording_session_)
            return false;
        active_recording_id_.clear();
        recording = std::move(recording_session_);
        finishing_recordings_.push_back(recording);
    }
    recording->Stop();
    return true;
}

bool NativeSession::StartVoiceCall() {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::VoiceCallController> voice_call{};
    {
        std::lock_guard state_lock(lifecycle_mutex_);
        if (stopped_.load() || !started_ || !voice_call_) {
            return false;
        }
        voice_call = voice_call_;
    }
    return voice_call->Start();
}

bool NativeSession::StopVoiceCall() {
    std::lock_guard command_lock(command_mutex_);
    std::shared_ptr<px::VoiceCallController> voice_call{};
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
    std::shared_ptr<px::VoiceCallController> voice_call{};
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
    std::shared_ptr<px::VoiceCallController> voice_call{};
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
    std::vector<std::shared_ptr<px::RecordingSession>> recordings{};
    std::shared_ptr<NativeClipboard> clipboard;
    std::shared_ptr<px::VoiceCallController> voice_call{};
    std::shared_ptr<ANativeWindow> surface;
    active_recording_id_.clear();
    {
        std::lock_guard lock(lifecycle_mutex_);
        sdk = std::move(sdk_);
        file_transfer_session = std::move(file_transfer_session_);
        recordings = std::move(finishing_recordings_);
        if (recording_session_)
            recordings.push_back(std::move(recording_session_));
        clipboard = std::move(clipboard_);
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
    for (const auto& recording : recordings)
        recording->Stop();
    for (const auto& recording : recordings)
        static_cast<void>(recording->WaitFor(std::chrono::seconds(5)));
    recordings.clear();
    if (clipboard) {
        clipboard->Stop();
    }
    if (voice_call) {
        // The SDK transport is about to close and stopped_ already rejects new
        // asynchronous sends. Tear the media endpoint down deterministically;
        // the peer observes the session transport closing.
        voice_call->Close();
    }
    if (sdk)
        sdk->Exit();
    if (decoder_output_) decoder_output_->Replace({});
    audio_player_->Stop();
}

} // namespace pixels::android
