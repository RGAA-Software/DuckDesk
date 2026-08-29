#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_data_provider_plugin.h"

namespace px {
namespace {

TEST(WasAudioPluginDllLifecycle, TenStartStopDestroyAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_WAS_AUDIO_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();

            // This is the established loader-owned plug-in instance ABI and
            // is explicitly excluded from the smart-pointer migration gate.
            using GetInstanceFn = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary)
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary)
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(
                *static_cast<PxDataProviderPlugin*>(
                    get_instance())); // NOLINT(gammaray-raw-pointer-boundary)

            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("cap_was_audio.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["device_id"] =
                "audio-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});
            PxPluginSettingsInfo settings;
            settings.audio_enabled_ = true;
            plugin.get().OnSyncPluginSettingsInfo(settings);

            plugin.get().StartProviding();
            ASSERT_EQ(plugin.get().GetLastStartError(), 0) << "round " << round;
            ASSERT_TRUE(plugin.get().IsProviding()) << "round " << round;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            const auto stop_started = std::chrono::steady_clock::now();
            plugin.get().StopProviding();
            EXPECT_FALSE(plugin.get().IsProviding()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
            EXPECT_LT(std::chrono::steady_clock::now() - stop_started,
                      std::chrono::seconds(5))
                << "round " << round;
        }

        EXPECT_EQ(GetModuleHandleW(L"cap_was_audio.dll"), nullptr)
            << "audio module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px
