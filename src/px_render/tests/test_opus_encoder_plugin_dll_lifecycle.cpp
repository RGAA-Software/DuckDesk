#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/data.h"
#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_audio_encoder_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px {
namespace {

TEST(OpusEncoderPluginDllLifecycle, TenEncodeDestroyAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_OPUS_ENCODER_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;
    const auto pcm = Data::From(std::string(480 * 2 * 2, '\0'));

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();
            using GetInstanceFn = void* (*)();  // NOLINT(gammaray-raw-pointer-boundary)
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance"));  // NOLINT(gammaray-raw-pointer-boundary)
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(*static_cast<PxAudioEncoderPlugin*>(
                get_instance()));  // NOLINT(gammaray-raw-pointer-boundary)

            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("enc_opus.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["save_debug_file"] = false;
            parameters.cluster_["device_id"] =
                "opus-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            std::atomic<int> encoded_events = 0;
            plugin.get().RegisterEventCallback(
                [&encoded_events](const std::shared_ptr<PxPluginBaseEvent>& event) {
                    if (std::dynamic_pointer_cast<PxPluginEncodedAudioFrameEvent>(event)) {
                        ++encoded_events;
                    }
                });
            plugin.get().Encode(pcm, 48000, 2, 16);
            plugin.get().Encode(pcm, 48000, 2, 16);
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(2);
            while (encoded_events.load() == 0 &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            EXPECT_EQ(encoded_events.load(), 1) << "round " << round;
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
        }
        EXPECT_EQ(GetModuleHandleW(L"enc_opus.dll"), nullptr)
            << "Opus encoder module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px
