#include <gtest/gtest.h>
#include "../win32/dxgi_mon_detector.h"
#include <algorithm>

using namespace tc;

TEST(DxgiMonDetectorTest, DetectAdapters_ReturnsNonEmpty) {
    DxgiMonitorDetector::Instance()->DetectAdapters();
    auto adapters = DxgiMonitorDetector::Instance()->GetAdapters();
    ASSERT_FALSE(adapters.empty()) << "DetectAdapters should return at least one monitor";
}

TEST(DxgiMonDetectorTest, PrimaryMonitor_IsUnique) {
    DxgiMonitorDetector::Instance()->DetectAdapters();
    auto adapters = DxgiMonitorDetector::Instance()->GetAdapters();

    int primary_count = std::count_if(adapters.begin(), adapters.end(), [](const DxgiMonInfo& info) {
        return info.primary;
    });

    EXPECT_EQ(primary_count, 1) << "There should be exactly one primary monitor, found " << primary_count;
}

TEST(DxgiMonDetectorTest, AllMonitors_HaveValidDimensions) {
    DxgiMonitorDetector::Instance()->DetectAdapters();
    auto adapters = DxgiMonitorDetector::Instance()->GetAdapters();

    for (const auto& info : adapters) {
        EXPECT_TRUE(info.IsValid()) << "Monitor " << info.display_name << " has invalid dimensions";
    }
}
