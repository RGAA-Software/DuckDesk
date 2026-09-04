#include <Windows.h>

#include <chrono>
#include <filesystem>
#include <future>
#include <memory>
#include <string>
#include <thread>

#include <gtest/gtest.h>

#include "px_render/network/webrtc_library_host.h"

namespace px {
namespace {

using namespace std::chrono_literals;

PxAwaitable<void> CollectWebRtcStop(
    const std::shared_ptr<WebRtcLibrary>& library,
    const std::shared_ptr<std::promise<PxResult<void>>>& completion) {
    completion->set_value(co_await WebRtcLibrary::StopAsync(
        library, std::chrono::steady_clock::now() + 5s));
}

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

void RunLifecycleRounds(const std::filesystem::path& rtc_path,
                        const std::filesystem::path& rtc_local_path) {
    const auto runtime = PxAsyncRuntime::Create({.worker_threads = 1});
    ASSERT_TRUE(runtime->Start());
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kControl);
    ASSERT_TRUE(scope);
    for (int round = 0; round < 10; ++round) {
        const ScopedWebRtcLayout layout(rtc_path, rtc_local_path);
        auto host = WebRtcLibraryHost::Create(layout.Directory());
        auto libraries = host->Load();
        ASSERT_EQ(libraries.size(), 2U) << "round " << round;

        const WebRtcLibraryConfiguration configuration{
            .base_path = layout.Directory().generic_string(),
            .base_data_path = std::filesystem::temp_directory_path().wstring(),
            .device_id = "rtc-lifecycle-" + std::to_string(round),
            .language = 0,
            .appkey = "lifecycle-test-key",
        };
        for (const auto& library : libraries) {
            ASSERT_TRUE(library->Start(configuration)) << "round " << round;
            library->SetEventCallback(
                [](const std::shared_ptr<PxPluginBaseEvent>&) {});
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));

        const auto stop_started = std::chrono::steady_clock::now();
        for (const auto& library : libraries) {
            const auto completion = std::make_shared<std::promise<PxResult<void>>>();
            auto future = completion->get_future();
            ASSERT_TRUE(scope->Spawn("test-webrtc-stop", [library, completion] {
                return CollectWebRtcStop(library, completion);
            }));
            ASSERT_EQ(future.wait_for(6s), std::future_status::ready);
            const auto stopped = future.get();
            if (!stopped) {
                ADD_FAILURE() << stopped.Error().StableCode() << ": " << stopped.Error().message;
            }
        }
        libraries.clear();
        host->Reset();
        host.reset();
        EXPECT_LT(std::chrono::steady_clock::now() - stop_started,
                  std::chrono::seconds(5)) << "round " << round;
        EXPECT_EQ(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
        EXPECT_EQ(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);
    }
    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    runtime->RequestDrain();
    runtime->Join();
}

TEST(WebRtcLibrariesLifecycle, RapidStartStopAndUnload) {
    RunLifecycleRounds(PX_WEBRTC_REMOTE_LIBRARY_PATH,
                       PX_WEBRTC_LOCAL_LIBRARY_PATH);
}

TEST(WebRtcLibrariesLifecycle, HostKeepsLibrariesAliveUntilLastOwnerRelease) {
    const auto rtc_path =
        std::filesystem::path(PX_WEBRTC_REMOTE_LIBRARY_PATH);
    const auto rtc_local_path =
        std::filesystem::path(PX_WEBRTC_LOCAL_LIBRARY_PATH);
    const ScopedWebRtcLayout layout(rtc_path, rtc_local_path);

    auto host = WebRtcLibraryHost::Create(layout.Directory());
    auto libraries = host->Load();
    ASSERT_EQ(libraries.size(), 2U);
    EXPECT_EQ(libraries[0]->Kind(), WebRtcLibraryKind::kRemote);
    EXPECT_EQ(libraries[0]->BaseName(), "net_rtc");
    EXPECT_EQ(libraries[1]->Kind(), WebRtcLibraryKind::kLocal);
    EXPECT_EQ(libraries[1]->BaseName(), "net_rtc_local");

    host->Reset();
    host.reset();

    // Concrete library owners retain the DLL handles even after the host
    // itself has been destroyed.
    EXPECT_NE(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
    EXPECT_NE(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);

    libraries.clear();
    EXPECT_EQ(GetModuleHandleW(rtc_path.filename().c_str()), nullptr);
    EXPECT_EQ(GetModuleHandleW(rtc_local_path.filename().c_str()), nullptr);
}

} // namespace
} // namespace px
