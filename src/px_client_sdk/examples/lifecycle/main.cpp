#include <px_client_sdk/sdk_net_client.h>
#include <px_common/message_notifier.h>
#include <iostream>
#include <memory>

#if !defined(PIXELS_EXAMPLE_CORE_ONLY)
#include <px_client_sdk/thunder_sdk.h>
#include <px_client_sdk/sdk_voice_call.h>
#if defined(__ANDROID__)
#include <px_client_sdk/platform/android/android_decoder_factory.h>
#include <px_client_sdk/platform/android/android_video_output.h>
#else
#include <px_client_sdk/platform/windows/windows_decoder_factory.h>
#include <px_client_sdk/platform/windows/windows_video_resources.h>
#endif
#endif

int main() {
    // Deliberately offline: no embedded ticket, account, server or fake decoder.
    // Init owns a real transport/platform factory; Exit must also work before Start.
    for (int iteration{}; iteration < 3; ++iteration) {
        const auto notifier = std::make_shared<px::MessageNotifier>();
#if defined(PIXELS_EXAMPLE_CORE_ONLY)
        auto session = std::make_shared<px::NetClient>(px::SdkConnectionParams{}, notifier);
        session->Exit();
        session->Exit();
#else
        auto params = std::make_shared<px::ThunderSdkParams>();
        params->device_name_ = "Pixels SDK consumer";
#if defined(__ANDROID__)
        params->client_type_ = px::kAndroid;
        auto factory = px::MakeAndroidVideoDecoderFactory(std::make_shared<px::AndroidVideoOutput>(nullptr), false);
#else
        params->client_type_ = px::kWindows;
        auto factory = px::MakeWindowsVideoDecoderFactory(std::make_shared<px::WindowsVideoResources>());
#endif
        auto session = px::ThunderSdk::Make(notifier);
        if (!session->Init(params, std::move(factory))) {
            std::cerr << "SDK initialization failed\n";
            return 1;
        }
        session->RefreshVideoOutput(false);
        // Link the real shared controller without installing a device engine. This offline example never starts a call.
        px::VoiceCallDependencies voice_dependencies{
            .send_control = [](std::shared_ptr<px::Message>) { return false; },
            .send_audio = [](std::shared_ptr<px::Message>) { return false; },
            .create_audio = []() -> std::shared_ptr<px::VoiceAudioPort> { return {}; },
            .post_task = [](std::function<void()>) { return false; },
            .status_changed = [](const px::VoiceCallStatus&) {},
        };
        const auto voice = px::VoiceCallController::Create({"offline-client", "offline-stream"}, std::move(voice_dependencies));
        if (!voice) {
            std::cerr << "Voice controller construction failed\n";
            return 1;
        }
        voice->Close();
        voice->Close();
        session->Exit();
        session->Exit();
#endif
        session.reset();
        notifier->Stop(px::MessageBusStopMode::kCancel);
    }
    std::cout << "Pixels SDK offline lifecycle OK\n";
    return 0;
}
