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

TEST(MediaRecorderPluginDllLifecycle, TenStartStopDestroyAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_MEDIA_RECORDER_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();

            // Established loader-owned plug-in instance ABI exception.
            using GetInstanceFn = void* (*)();  // NOLINT(gammaray-raw-pointer-boundary)
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance"));  // NOLINT(gammaray-raw-pointer-boundary)
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(*static_cast<PxStreamPlugin*>(
                get_instance()));  // NOLINT(gammaray-raw-pointer-boundary)

            const auto test_dir = std::filesystem::temp_directory_path() /
                ("px-media-recorder-lifecycle-" + std::to_string(round));
            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("media_recorder.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] = test_dir.wstring();
            parameters.cluster_["record_dir"] = test_dir.generic_string();
            parameters.cluster_["record_auto_enabled"] = false;
            parameters.cluster_["record_max_segment_bytes"] = int64_t{1024 * 1024};
            parameters.cluster_["record_max_file_count"] = int64_t{2};
            parameters.cluster_["device_id"] =
                "media-recorder-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});
            plugin.get().OnCommand("record:start");
            plugin.get().OnCommand("record:stop");
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
        }

        EXPECT_EQ(GetModuleHandleW(L"media_recorder.dll"), nullptr)
            << "media recorder module remained loaded after round " << round;
    }
}

}  // namespace
}  // namespace px
