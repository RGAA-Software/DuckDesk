#include "native_session.h"

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

jboolean NativeSendPointer(JNIEnv*, jobject, const jlong native_session_id, const jint action, // NOLINT(gammaray-raw-pointer-boundary)
                           const jfloat x_ratio, const jfloat y_ratio) {
    const auto session = Registry().Find(native_session_id);
    return session && session->SendPointer(action, x_ratio, y_ratio) ? JNI_TRUE : JNI_FALSE;
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

jboolean NativeSetAudioEnabled(JNIEnv*, jobject, const jlong native_session_id, const jboolean enabled) { // NOLINT(gammaray-raw-pointer-boundary)
    const auto session = Registry().Find(native_session_id);
    return session && session->SetAudioEnabled(enabled == JNI_TRUE) ? JNI_TRUE : JNI_FALSE;
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
        {const_cast<char*>("sendPointer"), const_cast<char*>("(JIFF)Z"), reinterpret_cast<void*>(pixels::android::NativeSendPointer)},
        {const_cast<char*>("sendText"), const_cast<char*>("(J[B)Z"), reinterpret_cast<void*>(pixels::android::NativeSendText)},
        {const_cast<char*>("setAudioEnabled"), const_cast<char*>("(JZ)Z"), reinterpret_cast<void*>(pixels::android::NativeSetAudioEnabled)},
        {const_cast<char*>("stop"), const_cast<char*>("(J)V"), reinterpret_cast<void*>(pixels::android::NativeStop)},
    };
    const auto result = environment->RegisterNatives(bridge_class, methods, std::size(methods));
    environment->DeleteLocalRef(bridge_class);
    return result == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
