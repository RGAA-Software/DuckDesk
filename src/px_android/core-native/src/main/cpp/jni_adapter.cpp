#include "native_session.h"
#include "native_clipboard.h"

#include <android/native_window_jni.h>
#include <jni.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace pixels::android {
namespace {

class SessionRegistry final {
  public:
    std::int64_t Add(std::shared_ptr<NativeSession> session) {
        std::lock_guard lock(mutex_);
        const auto id = next_id_++;
        sessions_.emplace(id, std::move(session));
        return id;
    }

    std::shared_ptr<NativeSession> Find(const std::int64_t id) const {
        std::lock_guard lock(mutex_);
        const auto iterator = sessions_.find(id);
        return iterator == sessions_.end() ? std::shared_ptr<NativeSession>{} : iterator->second;
    }

    std::shared_ptr<NativeSession> Remove(const std::int64_t id) {
        std::lock_guard lock(mutex_);
        const auto node = sessions_.extract(id);
        return node.empty() ? std::shared_ptr<NativeSession>{} : std::move(node.mapped());
    }

  private:
    mutable std::mutex mutex_{};
    std::unordered_map<std::int64_t, std::shared_ptr<NativeSession>> sessions_{};
    std::int64_t next_id_{1};
};

SessionRegistry& Registry() {
    static SessionRegistry registry{};
    return registry;
}

std::string ReadString(JNIEnv& environment, const jobject config, const jclass config_class, const std::string_view field_name) {
    const auto field = environment.GetFieldID(config_class, field_name.data(), "Ljava/lang/String;");
    if (field == nullptr) {
        return {};
    }
    const auto value = static_cast<jstring>(environment.GetObjectField(config, field));
    if (value == nullptr) {
        return {};
    }
    const char* characters = environment.GetStringUTFChars(value, nullptr); // NOLINT(gammaray-raw-pointer-boundary)
    const std::string result = characters == nullptr ? std::string{} : std::string{characters};
    if (characters != nullptr) {
        environment.ReleaseStringUTFChars(value, characters);
    }
    environment.DeleteLocalRef(value);
    return result;
}

std::int32_t ReadInt(JNIEnv& environment, const jobject config, const jclass config_class, const std::string_view field_name) {
    const auto field = environment.GetFieldID(config_class, field_name.data(), "I");
    return field == nullptr ? 0 : environment.GetIntField(config, field);
}

bool ReadBoolean(JNIEnv& environment, const jobject config, const jclass config_class, const std::string_view field_name) {
    const auto field = environment.GetFieldID(config_class, field_name.data(), "Z");
    return field != nullptr && environment.GetBooleanField(config, field) == JNI_TRUE;
}

std::string ReadUtf8Bytes(JNIEnv& environment, const jbyteArray value, const jsize maximum_size = 4096) {
    if (value == nullptr) {
        return {};
    }
    const auto length = environment.GetArrayLength(value);
    if (length <= 0 || length > maximum_size) {
        return {};
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    environment.GetByteArrayRegion(value, 0, length,
                                   reinterpret_cast<jbyte*>(result.data())); // NOLINT(gammaray-raw-pointer-boundary)
    return environment.ExceptionCheck() ? std::string{} : result;
}

std::string ReadJavaString(JNIEnv& environment, const jstring value, const std::size_t maximum_size = 4096U) {
    if (value == nullptr) {
        return {};
    }
    const char* characters = environment.GetStringUTFChars(value, nullptr); // NOLINT(gammaray-raw-pointer-boundary)
    const std::string result = characters == nullptr ? std::string{} : std::string{characters};
    if (characters != nullptr) {
        environment.ReleaseStringUTFChars(value, characters);
    }
    return result.size() <= maximum_size ? result : std::string{};
}

std::vector<std::string> ReadStringArray(JNIEnv& environment, const jobjectArray values, const jsize maximum_count) {
    if (values == nullptr) {
        return {};
    }
    const auto count = environment.GetArrayLength(values);
    if (count <= 0 || count > maximum_count) {
        return {};
    }
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(count));
    for (jsize index = 0; index < count; ++index) {
        const auto value_handle = reinterpret_cast<std::uintptr_t>(environment.GetObjectArrayElement(values, index));
        const auto value = ReadJavaString(environment, reinterpret_cast<jstring>(value_handle));
        environment.DeleteLocalRef(reinterpret_cast<jobject>(value_handle));
        if (value.empty()) {
            return {};
        }
        result.push_back(value);
    }
    return result;
}

NativeSessionConfig ReadConfig(JNIEnv& environment, const jobject config) {
    const auto config_class = environment.GetObjectClass(config);
    NativeSessionConfig result{
        .session_id = ReadString(environment, config, config_class, "sessionId"),
        .host = ReadString(environment, config, config_class, "host"),
        .port = ReadInt(environment, config, config_class, "port"),
        .ssl = ReadBoolean(environment, config, config_class, "ssl"),
        .remote_device_id = ReadString(environment, config, config_class, "remoteDeviceId"),
        .display_name = ReadString(environment, config, config_class, "displayName"),
        .stream_id = ReadString(environment, config, config_class, "streamId"),
        .client_device_id = ReadString(environment, config, config_class, "clientDeviceId"),
        .random_password = ReadString(environment, config, config_class, "randomPassword"),
        .connection_ticket = ReadString(environment, config, config_class, "connectionTicket"),
        .connection_nonce = ReadString(environment, config, config_class, "connectionNonce"),
        .rtc_ice_config_json = ReadString(environment, config, config_class, "rtcIceConfigJson"),
        .relay_host = ReadString(environment, config, config_class, "relayHost"),
        .relay_port = ReadInt(environment, config, config_class, "relayPort"),
        .enable_video = ReadBoolean(environment, config, config_class, "enableVideo"),
        .enable_audio = ReadBoolean(environment, config, config_class, "enableAudio"),
        .enable_input = ReadBoolean(environment, config, config_class, "enableInput"),
        .enable_clipboard = ReadBoolean(environment, config, config_class, "enableClipboard"),
    };
    environment.DeleteLocalRef(config_class);
    return result;
}

jlong NativeCreate(JNIEnv* environment, jobject, jobject config, jobject listener, jobject surface) { // NOLINT(gammaray-raw-pointer-boundary)
    if (environment == nullptr || config == nullptr || listener == nullptr || surface == nullptr) {
        return 0;
    }
    auto callback = JavaSessionCallback::Create(*environment, listener);
    std::unique_ptr<ANativeWindow, NativeWindowReleaser> native_window{ANativeWindow_fromSurface(environment, surface)};
    auto session = NativeSession::Create(ReadConfig(*environment, config), std::move(callback), std::move(native_window));
    return session ? Registry().Add(std::move(session)) : 0;
}

jboolean NativeStart(JNIEnv*, jobject, const jlong native_session_id) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->Start() ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeReplaceSurface(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                              jobject surface) {
    if (environment == nullptr || surface == nullptr) {
        return JNI_FALSE;
    }
    const auto session = Registry().Find(native_session_id);
    if (!session) {
        return JNI_FALSE;
    }
    std::unique_ptr<ANativeWindow, NativeWindowReleaser> native_window{ANativeWindow_fromSurface(environment, surface)};
    return session->RebindSurface(std::move(native_window)) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeDetachSurface(JNIEnv*, jobject, const jlong native_session_id) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->DetachSurface() ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendMouse(JNIEnv*, jobject, const jlong native_session_id, const jint action, const jint button, const jboolean down,
                         const jfloat x_ratio, const jfloat y_ratio, const jint delta_x,
                         const jint delta_y) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SendMouse(action, button, down == JNI_TRUE, x_ratio, y_ratio, delta_x, delta_y) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendKey(JNIEnv*, jobject, const jlong native_session_id, const jint virtual_key_code,
                       const jboolean down) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SendKey(virtual_key_code, down == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendText(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                        const jbyteArray utf8_text) {
    if (environment == nullptr || utf8_text == nullptr)
        return JNI_FALSE;
    const auto length = environment->GetArrayLength(utf8_text);
    if (length <= 0 || length > 4096)
        return JNI_FALSE;
    std::string text(static_cast<std::size_t>(length), '\0');
    environment->GetByteArrayRegion(utf8_text, 0, length,
                                    reinterpret_cast<jbyte*>(text.data())); // NOLINT(gammaray-raw-pointer-boundary)
    if (environment->ExceptionCheck())
        return JNI_FALSE;
    const auto session = Registry().Find(native_session_id);
    return session && session->SendText(text) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendClipboardText(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                                 const jbyteArray utf8_text) {
    if (environment == nullptr || utf8_text == nullptr)
        return JNI_FALSE;
    const auto length = environment->GetArrayLength(utf8_text);
    if (length <= 0 || length > 1'048'576)
        return JNI_FALSE;
    std::string text(static_cast<std::size_t>(length), '\0');
    environment->GetByteArrayRegion(utf8_text, 0, length,
                                    reinterpret_cast<jbyte*>(text.data())); // NOLINT(gammaray-raw-pointer-boundary)
    if (environment->ExceptionCheck())
        return JNI_FALSE;
    const auto session = Registry().Find(native_session_id);
    return session && session->SendClipboardText(text) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendClipboardFiles(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                                  const jstring generation, const jobjectArray display_names, const jobjectArray local_paths,
                                  const jlongArray sizes) {
    if (environment == nullptr || generation == nullptr || display_names == nullptr || local_paths == nullptr || sizes == nullptr) {
        return JNI_FALSE;
    }
    constexpr jsize kMaximumFiles = 16;
    const auto generation_value = ReadJavaString(*environment, generation, 128U);
    const auto names = ReadStringArray(*environment, display_names, kMaximumFiles);
    const auto paths = ReadStringArray(*environment, local_paths, kMaximumFiles);
    const auto size_count = environment->GetArrayLength(sizes);
    if (generation_value.empty() || names.empty() || names.size() != paths.size() || size_count != static_cast<jsize>(names.size())) {
        return JNI_FALSE;
    }
    std::vector<jlong> file_sizes(names.size());
    environment->GetLongArrayRegion(sizes, 0, size_count, file_sizes.data()); // NOLINT(gammaray-raw-pointer-boundary)
    if (environment->ExceptionCheck()) {
        return JNI_FALSE;
    }
    std::vector<NativeClipboardFile> files;
    files.reserve(names.size());
    for (std::size_t index = 0; index < names.size(); ++index) {
        files.push_back({.display_name = names[index], .backing_path = paths[index], .size = file_sizes[index]});
    }
    const auto session = Registry().Find(native_session_id);
    return session && session->SendClipboardFiles(generation_value, std::move(files)) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeDownloadClipboardFiles(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                                      const jstring generation, const jstring destination_directory) {
    if (environment == nullptr) {
        return JNI_FALSE;
    }
    const auto generation_value = ReadJavaString(*environment, generation, 128U);
    const auto destination_value = ReadJavaString(*environment, destination_directory);
    const auto session = Registry().Find(native_session_id);
    return session && session->DownloadClipboardFiles(generation_value, destination_value) ? JNI_TRUE : JNI_FALSE;
}

jint NativeStartFileUpload(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                           const jbyteArray local_path, const jbyteArray remote_directory) {
    if (environment == nullptr) {
        return 0;
    }
    const auto local = ReadUtf8Bytes(*environment, local_path);
    const auto remote = ReadUtf8Bytes(*environment, remote_directory);
    const auto session = Registry().Find(native_session_id);
    return session ? session->StartFileUpload(local, remote) : 0;
}

jint NativeStartFileDownload(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                             const jbyteArray remote_path, const jbyteArray local_directory) {
    if (environment == nullptr) {
        return 0;
    }
    const auto remote = ReadUtf8Bytes(*environment, remote_path);
    const auto local = ReadUtf8Bytes(*environment, local_directory);
    const auto session = Registry().Find(native_session_id);
    return session ? session->StartFileDownload(remote, local) : 0;
}

jboolean NativeCancelFileTransfer(JNIEnv*, jobject, const jlong native_session_id, const jint job_id) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->CancelFileTransfer(job_id) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeConfirmFileOverwrite(JNIEnv*, jobject, const jlong native_session_id, const jint job_id, const jint file_number,
                                    const jboolean overwrite, const jlong offset_bytes,
                                    const jboolean apply_to_all) { // NOLINT(gammaray-raw-pointer-boundary)
    if (offset_bytes < 0) {
        return JNI_FALSE;
    }
    const auto session = Registry().Find(native_session_id);
    return session && session->ConfirmFileOverwrite(job_id, file_number, overwrite == JNI_TRUE, static_cast<std::uint64_t>(offset_bytes),
                                                    apply_to_all == JNI_TRUE)
               ? JNI_TRUE
               : JNI_FALSE;
}

jboolean NativeSetAudioEnabled(JNIEnv*, jobject, const jlong native_session_id, const jboolean enabled) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SetAudioEnabled(enabled == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeStartRecording(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                              const jbyteArray recording_id, const jbyteArray staging_directory) {
    if (environment == nullptr) {
        return JNI_FALSE;
    }
    const auto id = ReadUtf8Bytes(*environment, recording_id, 128);
    const auto directory = ReadUtf8Bytes(*environment, staging_directory);
    const auto session = Registry().Find(native_session_id);
    return session && session->StartRecording(id, directory) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeStopRecording(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                             const jbyteArray recording_id) {
    if (environment == nullptr) {
        return JNI_FALSE;
    }
    const auto id = ReadUtf8Bytes(*environment, recording_id, 128);
    const auto session = Registry().Find(native_session_id);
    return session && session->StopRecording(id) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeStartVoiceCall(JNIEnv*, jobject, const jlong native_session_id) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->StartVoiceCall() ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeStopVoiceCall(JNIEnv*, jobject, const jlong native_session_id) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->StopVoiceCall() ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSetVoiceMicrophoneMuted(JNIEnv*, jobject, const jlong native_session_id,
                                       const jboolean muted) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SetVoiceMicrophoneMuted(muted == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSetVoiceSpeakerMuted(JNIEnv*, jobject, const jlong native_session_id,
                                    const jboolean muted) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SetVoiceSpeakerMuted(muted == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendSecureAttention(JNIEnv*, jobject, const jlong native_session_id) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SendSecureAttention() ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSendGamepad(JNIEnv*, jobject, const jlong native_session_id, const jint buttons, const jint left_trigger, const jint right_trigger,
                           const jint left_thumb_x, const jint left_thumb_y, const jint right_thumb_x,
                           const jint right_thumb_y) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    const NativeGamepadState state{
        .buttons = buttons,
        .left_trigger = left_trigger,
        .right_trigger = right_trigger,
        .left_thumb_x = left_thumb_x,
        .left_thumb_y = left_thumb_y,
        .right_thumb_x = right_thumb_x,
        .right_thumb_y = right_thumb_y,
    };
    return session && session->SendGamepad(state) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeSwitchMonitor(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                             const jstring monitor_name) {
    if (environment == nullptr || monitor_name == nullptr)
        return JNI_FALSE;
    const char* characters = environment->GetStringUTFChars(monitor_name, nullptr); // NOLINT(gammaray-raw-pointer-boundary)
    if (characters == nullptr)
        return JNI_FALSE;
    const std::string name{characters};
    environment->ReleaseStringUTFChars(monitor_name, characters);
    const auto session = Registry().Find(native_session_id);
    return session && session->SwitchMonitor(name) ? JNI_TRUE : JNI_FALSE;
}

jboolean NativeRequestVirtualDisplay(JNIEnv* environment, jobject, const jlong native_session_id, // NOLINT(gammaray-raw-pointer-boundary)
                                     const jstring request_id, const jint operation, const jint width, const jint height, const jint refresh_hz) {
    if (environment == nullptr || request_id == nullptr)
        return JNI_FALSE;
    const char* characters = environment->GetStringUTFChars(request_id, nullptr); // NOLINT(gammaray-raw-pointer-boundary)
    if (characters == nullptr)
        return JNI_FALSE;
    const std::string id{characters};
    environment->ReleaseStringUTFChars(request_id, characters);
    const auto session = Registry().Find(native_session_id);
    return session && session->RequestVirtualDisplay(id, operation, width, height, refresh_hz) ? JNI_TRUE : JNI_FALSE;
}

void NativeStop(JNIEnv*, jobject, const jlong native_session_id) { // NOLINT(gammaray-raw-pointer-boundary)
    if (const auto session = Registry().Remove(native_session_id)) {
        session->Stop();
    }
}

} // namespace
} // namespace pixels::android

extern "C" JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void*) { // NOLINT(gammaray-raw-pointer-boundary)
    if (vm == nullptr) {
        return JNI_ERR;
    }
    JNIEnv* environment = nullptr; // NOLINT(gammaray-raw-pointer-boundary)
    if (vm->GetEnv(reinterpret_cast<void**>(&environment), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    const auto bridge_class = environment->FindClass("yun/pixels/client/core/nativebridge/PixelsNativeBridge");
    if (bridge_class == nullptr) {
        return JNI_ERR;
    }
    const JNINativeMethod methods[]{
        {
            const_cast<char*>("create"),
            const_cast<char*>("(Lyun/pixels/client/core/nativebridge/NativeSessionConfig;"
                              "Lyun/pixels/client/core/nativebridge/NativeSessionListener;Landroid/view/Surface;)J"),
            reinterpret_cast<void*>(pixels::android::NativeCreate),
        },
        {const_cast<char*>("start"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(pixels::android::NativeStart)},
        {const_cast<char*>("replaceSurface"), const_cast<char*>("(JLandroid/view/Surface;)Z"),
         reinterpret_cast<void*>(pixels::android::NativeReplaceSurface)},
        {const_cast<char*>("detachSurface"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(pixels::android::NativeDetachSurface)},
        {const_cast<char*>("sendMouse"), const_cast<char*>("(JIIZFFII)Z"), reinterpret_cast<void*>(pixels::android::NativeSendMouse)},
        {const_cast<char*>("sendKey"), const_cast<char*>("(JIZ)Z"), reinterpret_cast<void*>(pixels::android::NativeSendKey)},
        {const_cast<char*>("sendText"), const_cast<char*>("(J[B)Z"), reinterpret_cast<void*>(pixels::android::NativeSendText)},
        {const_cast<char*>("sendClipboardText"), const_cast<char*>("(J[B)Z"), reinterpret_cast<void*>(pixels::android::NativeSendClipboardText)},
        {const_cast<char*>("sendClipboardFiles"), const_cast<char*>("(JLjava/lang/String;[Ljava/lang/String;[Ljava/lang/String;[J)Z"),
         reinterpret_cast<void*>(pixels::android::NativeSendClipboardFiles)},
        {const_cast<char*>("downloadClipboardFiles"), const_cast<char*>("(JLjava/lang/String;Ljava/lang/String;)Z"),
         reinterpret_cast<void*>(pixels::android::NativeDownloadClipboardFiles)},
        {const_cast<char*>("startFileUpload"), const_cast<char*>("(J[B[B)I"), reinterpret_cast<void*>(pixels::android::NativeStartFileUpload)},
        {const_cast<char*>("startFileDownload"), const_cast<char*>("(J[B[B)I"), reinterpret_cast<void*>(pixels::android::NativeStartFileDownload)},
        {const_cast<char*>("cancelFileTransfer"), const_cast<char*>("(JI)Z"), reinterpret_cast<void*>(pixels::android::NativeCancelFileTransfer)},
        {const_cast<char*>("confirmFileOverwrite"), const_cast<char*>("(JIIZJZ)Z"),
         reinterpret_cast<void*>(pixels::android::NativeConfirmFileOverwrite)},
        {const_cast<char*>("sendSecureAttention"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(pixels::android::NativeSendSecureAttention)},
        {const_cast<char*>("sendGamepad"), const_cast<char*>("(JIIIIIII)Z"), reinterpret_cast<void*>(pixels::android::NativeSendGamepad)},
        {const_cast<char*>("switchMonitor"), const_cast<char*>("(JLjava/lang/String;)Z"),
         reinterpret_cast<void*>(pixels::android::NativeSwitchMonitor)},
        {const_cast<char*>("requestVirtualDisplay"), const_cast<char*>("(JLjava/lang/String;IIII)Z"),
         reinterpret_cast<void*>(pixels::android::NativeRequestVirtualDisplay)},
        {const_cast<char*>("setAudioEnabled"), const_cast<char*>("(JZ)Z"), reinterpret_cast<void*>(pixels::android::NativeSetAudioEnabled)},
        {const_cast<char*>("startRecording"), const_cast<char*>("(J[B[B)Z"), reinterpret_cast<void*>(pixels::android::NativeStartRecording)},
        {const_cast<char*>("stopRecording"), const_cast<char*>("(J[B)Z"), reinterpret_cast<void*>(pixels::android::NativeStopRecording)},
        {const_cast<char*>("startVoiceCall"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(pixels::android::NativeStartVoiceCall)},
        {const_cast<char*>("stopVoiceCall"), const_cast<char*>("(J)Z"), reinterpret_cast<void*>(pixels::android::NativeStopVoiceCall)},
        {const_cast<char*>("setVoiceMicrophoneMuted"), const_cast<char*>("(JZ)Z"),
         reinterpret_cast<void*>(pixels::android::NativeSetVoiceMicrophoneMuted)},
        {const_cast<char*>("setVoiceSpeakerMuted"), const_cast<char*>("(JZ)Z"),
         reinterpret_cast<void*>(pixels::android::NativeSetVoiceSpeakerMuted)},
        {const_cast<char*>("stop"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(pixels::android::NativeStop)},
    };
    const auto result = environment->RegisterNatives(bridge_class, methods, std::size(methods));
    environment->DeleteLocalRef(bridge_class);
    return result == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
