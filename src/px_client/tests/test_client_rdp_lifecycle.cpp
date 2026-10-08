#include <gtest/gtest.h>

#include <asio2/websocket/ws_server.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include "client_session.h"
#include "px_message.pb.h"

namespace px::client::imgui {
namespace {

using namespace std::chrono_literals;

struct HeartbeatObservations final {
    std::mutex mutex{};
    std::condition_variable changed{};
    int received{};
    std::string deviceId{};
    std::string streamId{};
};

struct RdpTransportFixture final {
    std::shared_ptr<asio2::ws_server> server{std::make_shared<asio2::ws_server>()};
    std::shared_ptr<HeartbeatObservations> observations{std::make_shared<HeartbeatObservations>()};
    std::shared_ptr<ClientSession> client{};

    ~RdpTransportFixture() {
        if (client) client->Stop();
        server->stop();
    }

    bool StartServer() {
        const std::weak_ptr<HeartbeatObservations> weakObservations{observations};
        server->bind_recv([weakObservations](const auto&, const std::string_view payload) {
            Message envelope{};
            if (!envelope.ParseFromString(std::string{payload}) || envelope.type() != kHeartBeat || !envelope.has_heartbeat()) return;
            if (const auto current = weakObservations.lock()) {
                const std::scoped_lock lock{current->mutex};
                ++current->received;
                current->deviceId = envelope.device_id();
                current->streamId = envelope.stream_id();
                current->changed.notify_all();
            }
        });
        return server->start("127.0.0.1", 0);
    }

    bool WaitForHeartbeats(const int expected) const {
        const auto current = observations;
        std::unique_lock lock{current->mutex};
        return current->changed.wait_for(lock, 4s, [current, expected] { return current->received >= expected; });
    }

    ClientLaunchConfig Configuration() const {
        ClientLaunchConfig configuration{};
        configuration.host = "127.0.0.1";
        configuration.port = server->listen_port();
        configuration.localDeviceId = "rdp-lifecycle-client";
        configuration.streamId = "rdp-lifecycle-workspace";
        configuration.rdp = true;
        return configuration;
    }
};

TEST(ClientRdpLifecycle, IdleConnectionProducesHeartbeatsAndReleasesEachSession) {
    RdpTransportFixture fixture{};
    ASSERT_TRUE(fixture.StartServer());
    for (int visitIndex{}; visitIndex < 2; ++visitIndex) {
        fixture.client = ClientSession::Create(fixture.Configuration(), {});
        ASSERT_TRUE(fixture.client);
        fixture.client->Start();
        fixture.client->Start();
        ASSERT_TRUE(fixture.WaitForHeartbeats((visitIndex + 1) * 2));
        {
            const std::scoped_lock lock{fixture.observations->mutex};
            EXPECT_EQ(fixture.observations->deviceId, "rdp-lifecycle-client");
            EXPECT_EQ(fixture.observations->streamId, "rdp-lifecycle-workspace");
        }
        const std::weak_ptr<ClientSession> weakSession{fixture.client};
        fixture.client->Stop();
        fixture.client->Stop();
        fixture.client.reset();
        EXPECT_TRUE(weakSession.expired());
    }
}

TEST(ClientRdpLifecycle, DestructionBeforeStartIsSafe) {
    RdpTransportFixture fixture{};
    ASSERT_TRUE(fixture.StartServer());
    fixture.client = ClientSession::Create(fixture.Configuration(), {});
    ASSERT_TRUE(fixture.client);
    const std::weak_ptr<ClientSession> weakSession{fixture.client};
    fixture.client.reset();
    EXPECT_TRUE(weakSession.expired());
}

}  // namespace
}  // namespace px::client::imgui
