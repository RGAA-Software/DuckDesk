#include "client_panel_reporter.h"

#include <asio2/http/ws_client.hpp>
#include <mutex>
#include <string>

#include "client_session.h"
#include "px_client_panel_message.pb.h"
#include "px_common/log.h"
#include "px_common/url_helper.h"

namespace px::client::imgui {

struct ClientPanelReporter::State final {
    std::mutex mutex{};
    std::string streamId{};
    std::string pendingReport{};
    bool stopped{};
    bool started{};
};

struct ClientPanelReporter::Transport final {
    std::shared_ptr<asio2::ws_client> client{std::make_shared<asio2::ws_client>()};
    int port{};
    std::string path{};
};

ClientPanelReporter::ClientPanelReporter(const ClientLaunchConfig& config) {
    if (config.panelPort == 0 || config.panelLaunchId.empty()) return;
    state_ = std::make_shared<State>();
    state_->streamId = config.streamId;
    transport_ = std::make_unique<Transport>();
    const auto client = transport_->client;
    const std::weak_ptr<State> weakState{state_};
    const std::weak_ptr<asio2::ws_client> weakClient{client};
    client->set_connect_timeout(std::chrono::seconds{2});
    client->set_auto_reconnect(true, std::chrono::milliseconds{500});
    client->bind_connect([weakState, weakClient] {
        const auto state = weakState.lock();
        const auto connection = weakClient.lock();
        if (!state || !connection || !connection->is_started()) return;
        const std::scoped_lock lock{state->mutex};
        if (state->stopped) return;
        connection->ws_stream().binary(true);
        pxcp::CpMessage hello{};
        hello.set_type(pxcp::kCpHello);
        hello.set_stream_id(state->streamId);
        hello.mutable_hello()->set_type(pxcp::kWindowsClient);
        connection->async_send(hello.SerializeAsString());
        if (!state->pendingReport.empty()) connection->async_send(state->pendingReport);
    });
    transport_->port = config.panelPort;
    transport_->path = "/panel?stream_id=" + px::UrlHelper::EncodeQueryComponent(config.streamId) + "&launch_id=" + config.panelLaunchId;
}

ClientPanelReporter::~ClientPanelReporter() { Stop(); }

void ClientPanelReporter::Start() {
    if (!state_ || !transport_) return;
    {
        const std::scoped_lock lock{state_->mutex};
        if (state_->started || state_->stopped) return;
        state_->started = true;
    }
    transport_->client->async_start("127.0.0.1", transport_->port, transport_->path);
}

bool ClientPanelReporter::NeedsObservation() const {
    if (!state_) return false;
    const std::scoped_lock lock{state_->mutex};
    return !state_->stopped && state_->pendingReport.empty();
}

void ClientPanelReporter::Observe(const ClientSessionSnapshot& snapshot) {
    if (snapshot.state == ClientConnectionState::Connected) {
        Report(true, pxcp::kCpRejectionUnspecified);
    } else if (snapshot.state == ClientConnectionState::Rejected) {
        auto rejection = pxcp::kCpRejectionUnspecified;
        switch (snapshot.failure) {
            case ClientConnectionFailure::Authorization:
                rejection = pxcp::kCpRejectionAuthorization;
                break;
            case ClientConnectionFailure::Occupied:
                rejection = pxcp::kCpRejectionOccupied;
                break;
            case ClientConnectionFailure::RemoteAccessDisabled:
            case ClientConnectionFailure::SessionPolicy:
                rejection = pxcp::kCpRejectionSessionPolicy;
                break;
            default:
                break;
        }
        Report(false, rejection);
    }
}

void ClientPanelReporter::ReportInitializationFailure() { Report(false, pxcp::kCpRejectionUnspecified); }

void ClientPanelReporter::Report(const bool connected, const int rejection) {
    if (!state_ || !transport_) return;
    const std::scoped_lock lock{state_->mutex};
    if (state_->stopped || !state_->pendingReport.empty()) return;
    pxcp::CpMessage report{};
    report.set_type(connected ? pxcp::kCpTransportConnected : pxcp::kCpTransportRejected);
    report.set_stream_id(state_->streamId);
    if (!connected) report.mutable_transport_rejected()->set_reason(static_cast<pxcp::CpTransportRejection>(rejection));
    state_->pendingReport = report.SerializeAsString();
    LOGI("event=client.startup component=client stream={} outcome={} reason={}", state_->streamId, connected ? "connected" : "rejected", rejection);
    if (transport_->client->is_started()) transport_->client->async_send(state_->pendingReport);
}

void ClientPanelReporter::Stop() {
    if (!state_ || !transport_) return;
    {
        const std::scoped_lock lock{state_->mutex};
        if (state_->stopped) return;
        state_->stopped = true;
    }
    transport_->client->stop();
}

}  // namespace px::client::imgui
