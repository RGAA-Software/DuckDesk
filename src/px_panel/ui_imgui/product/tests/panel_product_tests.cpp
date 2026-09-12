#include "panel_audit_store.h"
#include "panel_connection_input.h"
#include "panel_connection_links.h"
#include "panel_config_store.h"
#include "panel_device_name.h"
#include "panel_worker.h"

#include "px_common/base64.h"
#include "px_common/shared_preference.h"
#include "px_render_panel_message.pb.h"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>
#include <future>

namespace px::panel::product {

TEST(PanelDeviceName, UsesPrivateIpv4LastSegmentWithMcPrefix) {
    EXPECT_EQ(BuildDefaultDeviceName({"203.0.113.8", "192.168.31.6"}), "MC-6");
    EXPECT_EQ(BuildDefaultDeviceName({"10.0.0.90"}), "MC-90");
}

TEST(PanelDeviceName, RecognizesOnlyNamesOwnedByTheAutomaticNamingPolicy) {
    EXPECT_TRUE(IsManagedDeviceName("D-90"));
    EXPECT_TRUE(IsManagedDeviceName("MC-6"));
    EXPECT_TRUE(IsManagedDeviceName("Pixels Node90"));
    EXPECT_FALSE(IsManagedDeviceName("Office Render"));
}
namespace {

class TemporaryDirectory final {
  public:
    TemporaryDirectory() {
        path_ =
            std::filesystem::temp_directory_path() / std::format("pixels-panel-test-{}", std::chrono::steady_clock::now().time_since_epoch().count());
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& Path() const {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

TEST(PanelAuditStoreTest, PersistsUpdatesAndDeletesRendererEvents) {
    TemporaryDirectory directory{};
    const auto store = PanelAuditStore::Create(directory.Path());
    ASSERT_TRUE(store);

    pxrp::RpMessage connected{};
    connected.set_type(pxrp::kRpClientConnected);
    auto& connection = *connected.mutable_client_connected();
    connection.set_conn_id("connection-1");
    connection.set_stream_id("stream-1");
    connection.set_conn_type("udp");
    connection.set_begin_timestamp(1000);
    connection.set_visitor_device_id("visitor");
    store->Consume(connected, "target");

    pxrp::RpMessage disconnected{};
    disconnected.set_type(pxrp::kRpClientDisConnected);
    disconnected.mutable_client_disconnected()->set_conn_id("connection-1");
    disconnected.mutable_client_disconnected()->set_end_timestamp(6000);
    disconnected.mutable_client_disconnected()->set_duration(5000);
    store->Consume(disconnected, "target");

    const auto visits = store->Query(ui::SecurityRecordKind::Visit);
    ASSERT_EQ(visits.size(), 1);
    EXPECT_TRUE(visits.front().succeeded);
    EXPECT_EQ(visits.front().visitor, "visitor");
    EXPECT_EQ(visits.front().target, "target");
    EXPECT_TRUE(store->Delete(ui::SecurityRecordKind::Visit, visits.front().id));
    EXPECT_TRUE(store->Query(ui::SecurityRecordKind::Visit).empty());

    pxrp::RpMessage begin{};
    begin.set_type(pxrp::kRpFileTransferBegin);
    begin.mutable_ft_begin()->set_the_file_id("file-1");
    begin.mutable_ft_begin()->set_begin_timestamp(7000);
    begin.mutable_ft_begin()->set_visitor_device_id("visitor");
    begin.mutable_ft_begin()->set_direction("send");
    begin.mutable_ft_begin()->set_file_detail("C:/data/example.txt");
    store->Consume(begin, "target");

    pxrp::RpMessage end{};
    end.set_type(pxrp::kRpFileTransferEnd);
    end.mutable_ft_end()->set_the_file_id("file-1");
    end.mutable_ft_end()->set_end_timestamp(9000);
    end.mutable_ft_end()->set_duration(2000);
    end.mutable_ft_end()->set_success(true);
    end.mutable_ft_end()->set_status("succeeded");
    store->Consume(end, "target");

    const auto transfers = store->Query(ui::SecurityRecordKind::FileTransfer);
    ASSERT_EQ(transfers.size(), 1);
    EXPECT_TRUE(transfers.front().succeeded);
    EXPECT_EQ(transfers.front().fileName, "example.txt");
    EXPECT_TRUE(store->DeleteAll(ui::SecurityRecordKind::FileTransfer));
    EXPECT_TRUE(store->Query(ui::SecurityRecordKind::FileTransfer).empty());
}

TEST(PanelWorkerTest, StopIsIdempotentAndRejectsNewWork) {
    const auto worker = PanelWorker::Create();
    ASSERT_TRUE(worker);
    std::promise<void> completed{};
    auto future = completed.get_future();
    EXPECT_TRUE(worker->Post([completion = std::make_shared<std::promise<void>>(std::move(completed))] { completion->set_value(); }));
    EXPECT_EQ(future.wait_for(std::chrono::seconds{2}), std::future_status::ready);
    worker->Stop();
    worker->Stop();
    EXPECT_FALSE(worker->Post([] {}));
}

TEST(PanelConfigStoreTest, PersistsAndClearsConnectionPreferences) {
    TemporaryDirectory directory{};
    const auto preferences = std::make_shared<SharedPreference>();
    ASSERT_TRUE(preferences->Init(directory.Path(), "preferences"));
    const auto config = std::make_shared<PanelConfigStore>(preferences, directory.Path());

    const RemoteDevicePreference remote{.name = "Office node",
                                        .audio = false,
                                        .clipboard = true,
                                        .viewOnly = true,
                                        .splitWindows = true,
                                        .forceSoftware = true,
                                        .forceTcp = true,
                                        .waitForDebugger = true,
                                        .forceGdiCapture = true,
                                        .disableVulkan = true};
    ASSERT_TRUE(config->SaveRemoteDevicePreference("device-1", remote));
    const auto loadedRemote = config->LoadRemoteDevicePreference("device-1");
    ASSERT_TRUE(loadedRemote);
    EXPECT_EQ(loadedRemote->name, remote.name);
    EXPECT_FALSE(loadedRemote->audio);
    EXPECT_TRUE(loadedRemote->clipboard);
    EXPECT_TRUE(loadedRemote->viewOnly);
    EXPECT_TRUE(loadedRemote->forceTcp);

    ASSERT_TRUE(
        config->SaveRemoteDeviceHistory({.deviceId = "device-1", .name = "Office node", .host = "192.168.1.8", .port = 4601, .lastConnectedAt = 42}));
    const auto history = config->LoadRemoteDeviceHistory();
    ASSERT_EQ(history.size(), 1);
    EXPECT_EQ(history.front().deviceId, "device-1");
    EXPECT_EQ(history.front().host, "192.168.1.8");
    ASSERT_TRUE(config->HideRemoteDevice("device-1"));
    EXPECT_TRUE(config->RemoteDeviceHidden("device-1"));
    ASSERT_TRUE(config->UnhideRemoteDevice("device-1"));
    EXPECT_FALSE(config->RemoteDeviceHidden("device-1"));
    ASSERT_TRUE(config->HideRemoteDevice("device-1"));
    ASSERT_TRUE(config->SaveCustomDeviceName("Studio node"));
    EXPECT_TRUE(config->DeviceNameIsCustom());
    EXPECT_EQ(config->Identity().deviceName, "Studio node");

    ASSERT_TRUE(config->SaveCloudApplicationPreference("application-1", {.forceRelay = true}));
    const auto loadedApplication = config->LoadCloudApplicationPreference("application-1");
    EXPECT_FALSE(loadedApplication.forceTcp);
    EXPECT_TRUE(loadedApplication.forceRelay);

    config->Clear();
    EXPECT_FALSE(config->LoadRemoteDevicePreference("device-1"));
    EXPECT_TRUE(config->LoadRemoteDeviceHistory().empty());
    EXPECT_FALSE(config->RemoteDeviceHidden("device-1"));
    EXPECT_FALSE(config->DeviceNameIsCustom());
    EXPECT_FALSE(config->LoadCloudApplicationPreference("application-1").forceRelay);
}

TEST(PanelConnectionLinksTest, PreservesCompleteDesktopAndWebConnectionPayloads) {
    const PanelIdentity identity{.deviceId = "109022351", .deviceName = "Pixels Node90", .randomPassword = "temporary"};
    const NodePorts ports{};
    const ConsoleEndpoint endpoint{.host = "39.71.45.66", .port = 4600, .relayPort = 4605, .appKey = "app-key"};

    const auto links = BuildPanelConnectionLinks(identity, ports, endpoint, "39.71.45.66", {"192.168.1.8"});
    ASSERT_TRUE(links.desktop.starts_with("link://"));
    const auto desktopPayload = nlohmann::json::parse(Base64::Base64Decode(links.desktop.substr(7)));
    EXPECT_EQ(desktopPayload.at("did"), identity.deviceId);
    EXPECT_EQ(desktopPayload.at("dn"), identity.deviceName);
    EXPECT_EQ(desktopPayload.at("rpwd"), identity.randomPassword);
    EXPECT_EQ(desktopPayload.at("iidx"), 12);
    ASSERT_EQ(desktopPayload.at("ips").size(), 1);
    EXPECT_EQ(desktopPayload.at("ips").at(0).at("ip"), "39.71.45.66");
    EXPECT_EQ(desktopPayload.at("ppt"), ports.panel);
    EXPECT_EQ(desktopPayload.at("rdpt"), ports.desktop);
    EXPECT_EQ(desktopPayload.at("rlst"), endpoint.host);
    EXPECT_EQ(desktopPayload.at("rlpt"), endpoint.relayPort);
    EXPECT_EQ(desktopPayload.at("rlak"), endpoint.appKey);

    const std::string webPrefix{"http://39.71.45.66:4601/web/?c="};
    ASSERT_TRUE(links.web.starts_with(webPrefix));
    std::string webToken{links.web.substr(webPrefix.size())};
    for (auto& character : webToken) {
        if (character == '-') {
            character = '+';
        } else if (character == '_') {
            character = '/';
        }
    }
    while (webToken.size() % 4U != 0U) {
        webToken.push_back('=');
    }
    const auto webPayload = nlohmann::json::parse(Base64::Base64Decode(webToken));
    EXPECT_EQ(webPayload.at("d"), identity.deviceId);
    EXPECT_EQ(webPayload.at("p"), identity.randomPassword);
}

TEST(PanelConnectionLinksTest, UsesFirstLocalAddressWhenNoPublicAddressIsConfigured) {
    const PanelIdentity identity{.deviceId = "101", .deviceName = "Pixels", .randomPassword = "temporary"};
    const auto links = BuildPanelConnectionLinks(identity, NodePorts{}, std::nullopt, {}, {"192.168.1.8", "10.0.0.2"});

    EXPECT_TRUE(links.web.starts_with("http://192.168.1.8:4601/web/?c="));
    EXPECT_EQ(links.web.find("127.0.0.1"), std::string::npos);
    const auto desktopPayload = nlohmann::json::parse(Base64::Base64Decode(links.desktop.substr(7)));
    ASSERT_EQ(desktopPayload.at("ips").size(), 2);
    EXPECT_EQ(desktopPayload.at("ips").at(0).at("ip"), "192.168.1.8");
    EXPECT_EQ(desktopPayload.at("ips").at(1).at("ip"), "10.0.0.2");
    EXPECT_EQ(ResolveNodeAccessHost({}, {"192.168.1.8", "10.0.0.2"}), "192.168.1.8");
    EXPECT_EQ(ResolveNodeAccessHost("render.example.com", {"192.168.1.8"}), "render.example.com");
    EXPECT_TRUE(ResolveNodeAccessHost({}, {"127.0.0.1"}).empty());
}

TEST(PanelConnectionInputTest, DistinguishesDeviceLinkAndDirectEndpointInputs) {
    const auto device = ParseConnectionInput(" 109022351 ", 4601);
    ASSERT_TRUE(device);
    EXPECT_EQ(device->kind, ConnectionInputKind::DeviceId);
    EXPECT_EQ(device->deviceId, "109022351");

    const PanelIdentity identity{.deviceId = "109022351", .deviceName = "Pixels Node90", .randomPassword = "temporary"};
    const ConsoleEndpoint relay{.host = "39.71.45.66", .port = 4600, .relayPort = 4605, .appKey = "app-key"};
    const auto links = BuildPanelConnectionLinks(identity, NodePorts{}, relay, "39.71.45.66", {});
    const auto shared = ParseConnectionInput(links.desktop, 4601);
    ASSERT_TRUE(shared);
    EXPECT_EQ(shared->kind, ConnectionInputKind::SharedLink);
    EXPECT_EQ(shared->deviceId, identity.deviceId);
    EXPECT_EQ(shared->displayName, identity.deviceName);
    EXPECT_EQ(shared->password, identity.randomPassword);
    ASSERT_EQ(shared->hosts.size(), 1);
    EXPECT_EQ(shared->hosts.front(), "39.71.45.66");
    EXPECT_EQ(shared->port, 4601);
    EXPECT_EQ(shared->relayHost, "39.71.45.66");
    EXPECT_EQ(shared->relayPort, 4605);
    EXPECT_EQ(shared->relayDeviceId, "server_109022351");

    const auto direct = ParseConnectionInput("https://39.71.45.66:4613/path", 4601);
    ASSERT_TRUE(direct);
    EXPECT_EQ(direct->kind, ConnectionInputKind::DirectEndpoint);
    EXPECT_EQ(direct->hosts.front(), "39.71.45.66");
    EXPECT_EQ(direct->port, 4613);
    EXPECT_TRUE(ConnectionInputNeedsPassword("39.71.45.66"));
    EXPECT_TRUE(ConnectionInputNeedsPassword("109022351"));
    EXPECT_FALSE(ConnectionInputNeedsPassword(links.desktop));
}

} // namespace
} // namespace px::panel::product
