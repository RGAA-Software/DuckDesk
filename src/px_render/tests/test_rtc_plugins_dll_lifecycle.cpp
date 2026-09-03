#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_render/network/webrtc_library_host.h"
#include "px_render/plugin_interface/px_net_plugin.h"
#include "px_render/plugin_interface/px_plugin_interface.h"
#include "px_render/plugins/plugin_ids.h"

namespace px {
namespace {

class ScopedWebRtcLayout final {
public:
    ScopedWebRtcLayout(const std::filesystem::path& rtc_source,
                       const std::filesystem::path& rtc_local_source)
        : directory_(std::filesystem::temp_directory_path() /
              ("gammaray-webrtc-host-" +
               std::to_string(std::chrono::steady_clock::now()
                                  .time_since_epoch().count()))) {
        std::filesystem::create_directories(directory_);
        std::filesystem::copy_file(
            rtc_source, directory_ / rtc_source.filename(),
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(
            rtc_local_source, directory_ / rtc_local_source.filename(),
            std::filesystem::copy_options::overwrite_existing);
    }

    ~ScopedWebRtcLayout() {
        std::error_code ignored;
        std::filesystem::remove_all(directory_, ignored);
    }

    ScopedWebRtcLayout(const ScopedWebRtcLayout&) = delete;
    ScopedWebRtcLayout& operator=(const ScopedWebRtcLayout&) = delete;

    [[nodiscard]] const std::filesystem::path& Directory() const {
        return directory_;
    }

private:
    std::filesystem::path directory_;
};

void RunLifecycleRounds(const std::filesystem::path& dll_path) {
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
            parameters.cluster_["name"] = dll_path.filename().string();
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["device_id"] =
                "rtc-lifecycle-" + std::to_string(round);
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] =
                std::string("lifecycle-test-key");

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

        EXPECT_EQ(GetModuleHandleW(dll_path.filename().c_str()), nullptr)
            << dll_path << " remained loaded after round " << round;
    }
}

TEST(RtcPluginsDllLifecycle, FullRtcRapidStartStopAndUnload) {
    RunLifecycleRounds(PX_RTC_PLUGIN_DLL_PATH);
}

TEST(RtcPluginsDllLifecycle, LocalRtcRapidStartStopAndUnload) {
    RunLifecycleRounds(PX_RTC_LOCAL_PLUGIN_DLL_PATH);
}

TEST(RtcPluginsDllLifecycle, FixedHostKeepsLibrariesAliveUntilLastAliasRelease) {
    const auto rtc_path = std::filesystem::path(PX_RTC_PLUGIN_DLL_PATH);
    const auto rtc_local_path =
        std::filesystem::path(PX_RTC_LOCAL_PLUGIN_DLL_PATH);
    const ScopedWebRtcLayout layout(rtc_path, rtc_local_path);

    auto host = WebRtcLibraryHost::Create(layout.Directory());
    auto modules = host->Load();
    ASSERT_EQ(modules.size(), 2U);

    for (const auto& module : modules) {
        ASSERT_TRUE(module);
        PxPluginParam parameters;
        parameters.cluster_["name"] =
            module->GetPluginId() == kNetRtcPluginId
                ? rtc_path.filename().string()
                : rtc_local_path.filename().string();
        parameters.cluster_["base_path"] =
            rtc_path.parent_path().generic_string();
        parameters.cluster_["base_data_path"] =
            std::filesystem::temp_directory_path().wstring();
        parameters.cluster_["device_id"] = std::string("rtc-host-lifecycle");
        parameters.cluster_["language"] = int64_t{0};
        parameters.cluster_["appkey"] = std::string("host-lifecycle-key");
        ASSERT_TRUE(module->OnCreate(parameters));
        module->RegisterEventCallback(
            [](const std::shared_ptr<PxPluginBaseEvent>&) {});
    }

    for (const auto& module : modules) {
        EXPECT_TRUE(module->OnStop());
        EXPECT_TRUE(module->OnDestroy());
    }
    host->Reset();
    host.reset();

    // The returned aliases own the DynamicLibrary handles even after the host
    // itself has been destroyed.
    EXPECT_NE(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
    EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);

    modules.clear();
    EXPECT_EQ(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
    EXPECT_EQ(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);
}

} // namespace
} // namespace px
