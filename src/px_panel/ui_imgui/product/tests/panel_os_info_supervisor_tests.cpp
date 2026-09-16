#include "panel_os_info_supervisor.h"

#include <gtest/gtest.h>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

namespace px::panel::product {
namespace {

std::size_t LaunchCount(const std::filesystem::path& path) {
    std::ifstream launches{path};
    std::size_t count{};
    for (std::string line{}; std::getline(launches, line);)
        ++count;
    return count;
}

} // namespace

TEST(PanelOsInfoSupervisor, RestartsFailedChildAndStopIsIdempotent) {
    ASSERT_NE(std::getenv("PIXELS_OS_INFO_TEST_CHILD"), nullptr);
    const auto nonce = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto testDirectory = std::filesystem::temp_directory_path() / ("pixels-os-info-supervisor-" + std::to_string(nonce));
    ASSERT_TRUE(std::filesystem::create_directories(testDirectory));
    const auto child = testDirectory / "px_osinfo.exe";
    ASSERT_TRUE(std::filesystem::copy_file(std::filesystem::path{std::getenv("PIXELS_OS_INFO_TEST_CHILD")}, child));

    const auto supervisor = PanelOsInfoSupervisor::Create(testDirectory, 4999);
    ASSERT_TRUE(supervisor);
    const auto launchLog = testDirectory / "os_info_launches.txt";
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{6};
    while (LaunchCount(launchLog) < 2 && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    ASSERT_GE(LaunchCount(launchLog), 2U);

    supervisor->Stop();
    supervisor->Stop();
    const auto stoppedCount = LaunchCount(launchLog);
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    EXPECT_EQ(LaunchCount(launchLog), stoppedCount);

    std::filesystem::remove_all(testDirectory);
}

} // namespace px::panel::product
