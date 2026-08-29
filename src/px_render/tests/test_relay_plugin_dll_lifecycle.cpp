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

TEST(RelayPluginDllLifecycle, TenRapidStartStopAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_RELAY_PLUGIN_DLL_PATH);
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
            parameters.cluster_["name"] = std::string("net_relay.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["device_id"] =
                "relay-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_device_id"] = std::string();
            parameters.cluster_["relay_host"] = std::string("127.0.0.1");
            parameters.cluster_["relay_port"] = std::string("1");
            parameters.cluster_["relay_enabled"] = true;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] = std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            plugin.get().RegisterEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});

            PxPluginSettingsInfo settings;
            settings.device_id_ =
                "relay-lifecycle-" + std::to_string(round);
            settings.relay_host_ = "127.0.0.1";
            settings.relay_port_ = "1";
            settings.relay_enabled_ = true;
            settings.appkey_ = "lifecycle-test-key";
            plugin.get().OnSyncPluginSettingsInfo(settings);
            plugin.get().On1Second();

            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            settings.relay_port_ = round % 2 == 0 ? "2" : "1";
            settings.appkey_ += "-updated";
            plugin.get().OnSyncPluginSettingsInfo(settings);
            plugin.get().On1Second();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            const auto stop_started = std::chrono::steady_clock::now();
            EXPECT_TRUE(plugin.get().OnStop()) << "round " << round;
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
            EXPECT_LT(std::chrono::steady_clock::now() - stop_started,
                      std::chrono::seconds(5))
                << "round " << round;
        }

        EXPECT_EQ(GetModuleHandleW(L"net_relay.dll"), nullptr)
            << "relay module remained loaded after round " << round;
    }
}

} // namespace
} // namespace px
