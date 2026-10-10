#include <gtest/gtest.h>

#include <asio2/http/ws_client.hpp>
#include <filesystem>
#include <fstream>
#include <future>
#include <thread>

#include "application_launch_workflow.h"
#include "panel_local_server.h"
#include "px_client_panel_message.pb.h"
#include "px_common/shared_preference.h"
#include "px_common/uuid.h"
#include "ui_imgui/client_panel_reporter.h"
#include "ui_imgui/client_session.h"

namespace px::panel::product {
namespace {

class ClientStartupTest : public testing::Test {
protected:
    void SetUp() override {
        directory_ = std::filesystem::temp_directory_path() / ("pixels-startup-" + px::GetCanonicalUUID());
        std::filesystem::create_directories(directory_);
        {
            std::ofstream configFile{directory_ / "px_service.toml"};
            configFile << "[network]\npanel_port = 0\n";
        }
        preferences_ = std::make_shared<px::SharedPreference>();
        ASSERT_TRUE(preferences_->Init(directory_, "preferences"));
        config_ = std::make_shared<PanelConfigStore>(preferences_, directory_);
        server_ = PanelLocalServer::Create(config_, PanelAuditStore::Create(directory_ / "audit"));
        ASSERT_TRUE(server_->Snapshot().listening);
    }

    void TearDown() override {
        server_->Stop();
        server_.reset();
        config_.reset();
        preferences_->Release();
        preferences_.reset();
        std::error_code cleanupError{};
        std::filesystem::remove_all(directory_, cleanupError);
    }

    px::client::imgui::ClientLaunchConfig LaunchConfig() const {
        px::client::imgui::ClientLaunchConfig config{};
        config.streamId = "startup-test-stream";
        config.panelPort = server_->Snapshot().listenPort;
        config.panelLaunchId = px::GetCanonicalUUID();
        return config;
    }

    std::filesystem::path directory_{};
    std::shared_ptr<px::SharedPreference> preferences_{};
    std::shared_ptr<PanelConfigStore> config_{};
    std::shared_ptr<PanelLocalServer> server_{};
};

TEST_F(ClientStartupTest, LocalHelloDoesNotCompleteLaunchButRemoteConnectedDoes) {
    const auto config = LaunchConfig();
    const auto startup = std::make_shared<PanelClientStartup>(config.streamId);
    ASSERT_TRUE(server_->RegisterClientStartup(config.panelLaunchId, startup));
    px::client::imgui::ClientPanelReporter reporter{config};
    reporter.Start();
    reporter.Observe({.state = px::client::imgui::ClientConnectionState::Connecting});
    EXPECT_FALSE(startup->WaitFor(std::chrono::milliseconds{200}));
    reporter.Observe({.state = px::client::imgui::ClientConnectionState::Connected});
    const auto result = startup->WaitFor(std::chrono::seconds{3});
    ASSERT_TRUE(result);
    EXPECT_TRUE(result->connected);
    reporter.Stop();
    reporter.Stop();
}

TEST_F(ClientStartupTest, EarlyRemoteResultSurvivesLocalConnectionSetupAndFirstResultWins) {
    const auto config = LaunchConfig();
    const auto startup = std::make_shared<PanelClientStartup>(config.streamId);
    ASSERT_TRUE(server_->RegisterClientStartup(config.panelLaunchId, startup));
    px::client::imgui::ClientPanelReporter reporter{config};
    reporter.Observe({.state = px::client::imgui::ClientConnectionState::Rejected, .failure = px::client::imgui::ClientConnectionFailure::Occupied});
    reporter.Observe({.state = px::client::imgui::ClientConnectionState::Connected});
    reporter.Start();
    const auto result = startup->WaitFor(std::chrono::seconds{3});
    ASSERT_TRUE(result);
    EXPECT_FALSE(result->connected);
    EXPECT_EQ(result->error, px::ui::TextId::ConnectionRemoteSessionOccupied);
}

TEST_F(ClientStartupTest, WrongLaunchOrStreamCannotCompleteAnotherLaunch) {
    const auto config = LaunchConfig();
    const auto startup = std::make_shared<PanelClientStartup>(config.streamId);
    ASSERT_TRUE(server_->RegisterClientStartup(config.panelLaunchId, startup));
    auto wrongLaunch = config;
    wrongLaunch.panelLaunchId = px::GetCanonicalUUID();
    px::client::imgui::ClientPanelReporter staleReporter{wrongLaunch};
    staleReporter.Start();
    staleReporter.Observe({.state = px::client::imgui::ClientConnectionState::Connected});
    auto wrongStream = config;
    wrongStream.streamId = "another-stream";
    px::client::imgui::ClientPanelReporter otherReporter{wrongStream};
    otherReporter.Start();
    otherReporter.Observe({.state = px::client::imgui::ClientConnectionState::Connected});
    EXPECT_FALSE(startup->WaitFor(std::chrono::milliseconds{250}));
    server_->ForgetClientStartup(config.panelLaunchId);
    px::client::imgui::ClientPanelReporter forgottenReporter{config};
    forgottenReporter.Start();
    forgottenReporter.Observe({.state = px::client::imgui::ClientConnectionState::Connected});
    EXPECT_FALSE(startup->WaitFor(std::chrono::milliseconds{200}));
}

TEST_F(ClientStartupTest, ShutdownWakesWaitingLaunchAndLateReportsCannotOverwriteIt) {
    const auto config = LaunchConfig();
    const auto startup = std::make_shared<PanelClientStartup>(config.streamId);
    ASSERT_TRUE(server_->RegisterClientStartup(config.panelLaunchId, startup));
    const auto waiting = std::make_shared<std::promise<void>>();
    auto entered = waiting->get_future();
    auto result = std::async(std::launch::async, [startup, waiting] {
        waiting->set_value();
        return startup->WaitFor(std::chrono::seconds{10});
    });
    entered.wait();
    server_->Stop();
    ASSERT_EQ(result.wait_for(std::chrono::seconds{1}), std::future_status::ready);
    const auto stopped = result.get();
    ASSERT_TRUE(stopped);
    EXPECT_EQ(stopped->error, px::ui::TextId::ConnectionClientExited);
    EXPECT_FALSE(startup->Resolve(true));
    EXPECT_FALSE(server_->RegisterClientStartup(px::GetCanonicalUUID(), startup));
}

TEST_F(ClientStartupTest, DestructionWithQueuedConnectAndSendIsSafeAcrossRepeatedStarts) {
    for (int attempt{}; attempt < 12; ++attempt) {
        const auto config = LaunchConfig();
        const auto startup = std::make_shared<PanelClientStartup>(config.streamId);
        ASSERT_TRUE(server_->RegisterClientStartup(config.panelLaunchId, startup));
        {
            px::client::imgui::ClientPanelReporter reporter{config};
            reporter.Start();
            reporter.Observe({.state = px::client::imgui::ClientConnectionState::Connected});
            server_->ForgetClientStartup(config.panelLaunchId);
        }
    }
}

}  // namespace
}  // namespace px::panel::product
