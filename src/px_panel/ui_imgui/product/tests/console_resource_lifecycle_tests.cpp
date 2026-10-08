#include <Windows.h>
#include <gtest/gtest.h>

#include <array>
#include <string>

#include "px_common/http_client.h"
#include "px_common/uuid.h"
#include "px_console_client/console_api.h"
#include "px_console_client/console_resource_api.h"

namespace px::panel::product {
namespace {

TEST(ConsoleResourceLifecycle, OccupancyAndRetirementRemainDistinctProtocolErrors) {
    EXPECT_EQ(px_console::ToConsoleUserApiError(px::HttpResponse{.status = 409, .body = R"({"code":"gpu_memory_exhausted"})"}),
              px_console::ConsoleApiError::kGpuMemoryExhausted);
    EXPECT_EQ(px_console::ToConsoleUserApiError(px::HttpResponse{.status = 409, .body = R"({"code":"connection_busy"})"}),
              px_console::ConsoleApiError::kConnectionBusy);
    EXPECT_EQ(px_console::ToConsoleUserApiError(px::HttpResponse{.status = 409, .body = R"({"code":"connection_retiring"})"}),
              px_console::ConsoleApiError::kConnectionRetiring);
}

TEST(ConsoleResourceLifecycle, HttpsReservationFailuresAndRevisionChanges) {
    std::array<char, 16> fixturePort{};
    const auto portLength = GetEnvironmentVariableA("PIXELS_RESOURCE_TEST_PORT", fixturePort.data(), static_cast<DWORD>(fixturePort.size()));
    if (portLength == 0) GTEST_SKIP() << "Run through scripts/tests/run_console_resource_lifecycle.py";
    ASSERT_LT(portLength, fixturePort.size());
    const auto port = std::stoi(std::string{fixturePort.data(), portLength});
    std::array<char, 64> selectedScenario{};
    const auto scenarioLength =
        GetEnvironmentVariableA("PIXELS_RESOURCE_TEST_SCENARIO", selectedScenario.data(), static_cast<DWORD>(selectedScenario.size()));
    ASSERT_LT(scenarioLength, selectedScenario.size());
    const std::string scenarioFilter{selectedScenario.data(), scenarioLength};
    const px_console::ConsoleResourceTarget target{.kind = px_console::ConsoleResourceTargetKind::CloudApplication,
                                                   .application_id = "8ebdbd8e-925d-4885-9401-e87d0418bff9",
                                                   .instance_id = "93488646-70a1-42b3-9c4d-0123456789ab"};
    for (const std::string scenario : {"descriptor_failure", "malformed_descriptor", "invalid_descriptor", "invalid_open_metadata",
                                       "cleanup_rejected", "success", "stale_close", "concurrent_close", "already_closed", "close_rejected"}) {
        SCOPED_TRACE(scenario);
        if (!scenarioFilter.empty() && scenario != scenarioFilter) continue;
        px_console::SetConsoleApiLastErrorMessage({});
        const auto connection = px_console::OpenPanelResourceConnection("127.0.0.1", port, scenario, true, target, false, GetCanonicalUUID());
        if (scenario == "descriptor_failure" || scenario == "cleanup_rejected") {
            ASSERT_FALSE(connection);
            EXPECT_EQ(connection.error(), px_console::ConsoleApiError::kServiceUnavailable);
            EXPECT_EQ(px_console::ConsoleApiLastErrorMessage(), "descriptor unavailable");
        } else if (scenario == "malformed_descriptor" || scenario == "invalid_descriptor" || scenario == "invalid_open_metadata") {
            ASSERT_FALSE(connection);
            EXPECT_EQ(connection.error(), px_console::ConsoleApiError::kParseJsonFailed);
        } else {
            ASSERT_TRUE(connection);
            const auto closed =
                px_console::ClosePanelResourceConnection("127.0.0.1", port, scenario, true, connection->session_id, connection->session_revision);
            if (scenario == "close_rejected") {
                ASSERT_FALSE(closed);
                EXPECT_EQ(closed.error(), px_console::ConsoleApiError::kForbidden);
            } else {
                ASSERT_TRUE(closed);
                EXPECT_TRUE(*closed);
            }
        }
    }
}

}  // namespace
}  // namespace px::panel::product
