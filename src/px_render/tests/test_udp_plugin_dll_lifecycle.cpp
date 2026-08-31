#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_plugin_interface.h"

namespace px {
namespace {

TEST(UdpPluginDllLifecycle, TenRapidStartStopAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_UDP_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();

            using GetInstanceFn = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary): established plug-in ABI.
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary): transient plug-in symbol.
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(
                *static_cast<PxPluginInterface*>(get_instance())); // NOLINT(gammaray-raw-pointer-boundary): established plug-in instance ABI.

            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("net_udp.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["device_id"] =
                "udp-lifecycle-" + std::to_string(round);
            parameters.cluster_["udp-listen-port"] = int64_t{0};
            parameters.cluster_["fec-percent"] = int64_t{20};
            parameters.cluster_["mtu"] = int64_t{1400};
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});
            std::this_thread::sleep_for(std::chrono::milliseconds(20));

            const auto stop_started = std::chrono::steady_clock::now();
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
            EXPECT_LT(std::chrono::steady_clock::now() - stop_started,
                      std::chrono::seconds(5))
                << "round " << round;
        }

        EXPECT_EQ(GetModuleHandleW(L"net_udp.dll"), nullptr)
            << "UDP module remained loaded after round " << round;
    }
}

} // namespace
} // namespace px
