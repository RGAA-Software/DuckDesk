#include <Windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px {
namespace {

TEST(VoiceCallPluginDllLifecycle, TenCreateDestroyAndUnloadRounds) {
    const auto dll_path =
        std::filesystem::path(PX_VOICE_CALL_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();
            using GetInstanceFn = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(*static_cast<PxPluginInterface*>(
                get_instance())); // NOLINT(gammaray-raw-pointer-boundary): plug-in ABI

            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("voice_call.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["voice_call_enabled"] = true;
            parameters.cluster_["device_id"] =
                "voice-call-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});
            plugin.get().OnNewClientConnected(
                "visitor", "stream-" + std::to_string(round), "UDP");
            plugin.get().OnClientDisconnected(
                "visitor", "stream-" + std::to_string(round));
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
        }
        EXPECT_EQ(GetModuleHandleW(L"voice_call.dll"), nullptr)
            << "voice call module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px
