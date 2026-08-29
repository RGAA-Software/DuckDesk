#include <Windows.h>

#include <filesystem>
#include <functional>
#include <memory>
#include <string>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_stream_plugin.h"

namespace px {
namespace {

TEST(LivePusherPluginDllLifecycle, TenCreateDestroyAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_LIVE_PUSHER_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();
            using GetInstanceFn = void* (*)();  // NOLINT(gammaray-raw-pointer-boundary)
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance"));  // NOLINT(gammaray-raw-pointer-boundary)
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(*static_cast<PxStreamPlugin*>(
                get_instance()));  // NOLINT(gammaray-raw-pointer-boundary)

            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("live_pusher.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["push_enabled"] = true;
            parameters.cluster_["push_rtmp_url"] =
                std::string("rtmp://127.0.0.1:1/live/{live_stream_id}");
            parameters.cluster_["live_stream_id"] =
                "lifecycle-" + std::to_string(round);
            parameters.cluster_["push_primary_monitor"] = std::string{};
            parameters.cluster_["push_audio_bitrate"] = int64_t{96000};
            parameters.cluster_["device_id"] =
                "live-pusher-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
        }
        EXPECT_EQ(GetModuleHandleW(L"live_pusher.dll"), nullptr)
            << "live pusher module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px
