#include <Windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <functional>
#include <thread>

#include <gtest/gtest.h>

#include "px_common_new/win32/dynamic_library.h"
#include "px_render/plugin_interface/px_monitor_capture_plugin.h"
#include "px_render/plugin_interface/px_plugin_events.h"

namespace px {
namespace {

using namespace std::chrono_literals;

bool WaitForCapturedFrameCount(
    const std::shared_ptr<std::atomic_int>& captured_frames,
    int expected,
    std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (captured_frames->load() >= expected) {
            return true;
        }
        std::this_thread::sleep_for(5ms);
    }
    return captured_frames->load() >= expected;
}

TEST(GdiCapturePluginDllLifecycle, TenCaptureStopDestroyAndUnloadRounds) {
    const auto dll_path = std::filesystem::path(PX_GDI_CAPTURE_PLUGIN_DLL_PATH);
    ASSERT_TRUE(std::filesystem::is_regular_file(dll_path)) << dll_path;

    for (int round = 0; round < 10; ++round) {
        {
            DynamicLibrary library(dll_path.wstring());
            ASSERT_TRUE(library.Load())
                << "round " << round << ": " << library.GetErrorString();

            // Established loader-owned plug-in instance ABI exception.
            using GetInstanceFn = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary)
            const auto get_instance = reinterpret_cast<GetInstanceFn>(
                library.GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary)
            ASSERT_NE(get_instance, nullptr) << "round " << round;
            auto plugin = std::ref(
                *static_cast<PxMonitorCapturePlugin*>(
                    get_instance())); // NOLINT(gammaray-raw-pointer-boundary)

            PxPluginParam parameters;
            parameters.cluster_["name"] = std::string("cap_gdi.dll");
            parameters.cluster_["base_path"] =
                dll_path.parent_path().generic_string();
            parameters.cluster_["base_data_path"] =
                std::filesystem::temp_directory_path().wstring();
            parameters.cluster_["device_id"] =
                "gdi-lifecycle-" + std::to_string(round);
            parameters.cluster_["relay_enabled"] = false;
            parameters.cluster_["language"] = int64_t{0};
            parameters.cluster_["appkey"] =
                std::string("lifecycle-test-key");

            ASSERT_TRUE(plugin.get().OnCreate(parameters)) << "round " << round;
            const auto captured_frames = std::make_shared<std::atomic_int>(0);
            plugin.get().RegisterEventCallback(
                [captured_frames](const std::shared_ptr<PxPluginBaseEvent>& event) {
                    if (event && event->event_type_ ==
                                     PxPluginEventType::kPluginCapturedVideoFrameEvent) {
                        ++(*captured_frames);
                    }
                });
            plugin.get().SetCaptureFps(10);
            ASSERT_TRUE(plugin.get().StartCapturing()) << "round " << round;
            EXPECT_TRUE(WaitForCapturedFrameCount(captured_frames, 1, 2s))
                << "round " << round;

            const auto stop_started = std::chrono::steady_clock::now();
            // Destroy while capture is active. OnDestroy must stop and join
            // every worker before the DLL can unload.
            EXPECT_TRUE(plugin.get().OnDestroy()) << "round " << round;
            EXPECT_LT(std::chrono::steady_clock::now() - stop_started, 2s)
                << "round " << round;
        }

        EXPECT_EQ(GetModuleHandleW(L"cap_gdi.dll"), nullptr)
            << "GDI module remained loaded after round " << round;
    }
}

TEST(GdiCapturePluginDllLifecycle, RepeatedStartStopOnOneInstanceTenRounds) {
    const auto dll_path = std::filesystem::path(PX_GDI_CAPTURE_PLUGIN_DLL_PATH);
    DynamicLibrary library(dll_path.wstring());
    ASSERT_TRUE(library.Load()) << library.GetErrorString();

    using GetInstanceFn = void* (*)(); // NOLINT(gammaray-raw-pointer-boundary)
    const auto get_instance = reinterpret_cast<GetInstanceFn>(
        library.GetSymbol("GetInstance")); // NOLINT(gammaray-raw-pointer-boundary)
    ASSERT_NE(get_instance, nullptr);
    auto plugin = std::ref(
        *static_cast<PxMonitorCapturePlugin*>(
            get_instance())); // NOLINT(gammaray-raw-pointer-boundary)

    PxPluginParam parameters;
    parameters.cluster_["name"] = std::string("cap_gdi.dll");
    parameters.cluster_["base_path"] =
        dll_path.parent_path().generic_string();
    parameters.cluster_["base_data_path"] =
        std::filesystem::temp_directory_path().wstring();
    parameters.cluster_["device_id"] = std::string("gdi-repeated-lifecycle");
    parameters.cluster_["relay_enabled"] = false;
    parameters.cluster_["language"] = int64_t{0};
    parameters.cluster_["appkey"] = std::string("lifecycle-test-key");
    ASSERT_TRUE(plugin.get().OnCreate(parameters));

    const auto captured_frames = std::make_shared<std::atomic_int>(0);
    plugin.get().RegisterEventCallback(
        [captured_frames](const std::shared_ptr<PxPluginBaseEvent>& event) {
            if (event && event->event_type_ ==
                             PxPluginEventType::kPluginCapturedVideoFrameEvent) {
                ++(*captured_frames);
            }
        });
    plugin.get().SetCaptureFps(10);

    for (int round = 1; round <= 10; ++round) {
        ASSERT_TRUE(plugin.get().StartCapturing()) << "round " << round;
        EXPECT_TRUE(WaitForCapturedFrameCount(captured_frames, round, 2s))
            << "round " << round;
        const auto stop_started = std::chrono::steady_clock::now();
        plugin.get().StopCapturing();
        EXPECT_LT(std::chrono::steady_clock::now() - stop_started, 2s)
            << "round " << round;
    }

    EXPECT_TRUE(plugin.get().OnStop());
    EXPECT_TRUE(plugin.get().OnDestroy());
}

}  // namespace
}  // namespace px
