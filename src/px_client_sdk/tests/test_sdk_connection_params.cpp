#include "sdk_connection_params.h"
#include "sdk_net_client.h"
#include "sdk_messages.h"
#include "px_common/message_notifier.h"

#include <asio2/websocket/ws_server.hpp>
#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace px {
namespace {

using namespace std::chrono_literals;

bool WaitFor(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate())
            return true;
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

struct TransportObservations final {
    std::mutex mutex{};
    std::string target{};
    std::string device{};
    std::string stream{};
    std::atomic_bool ready{false};
};

struct TransportHarness final {
    std::shared_ptr<MessageNotifier> notifier{std::make_shared<MessageNotifier>()};
    std::shared_ptr<asio2::ws_server> server{std::make_shared<asio2::ws_server>()};
    std::shared_ptr<TransportObservations> observations{std::make_shared<TransportObservations>()};
    std::shared_ptr<NetClient> client{};

    ~TransportHarness() {
        if (client)
            client->Exit();
        server->stop();
        notifier->Stop(MessageBusStopMode::kCancel);
    }

    bool StartServer() {
        const std::weak_ptr<TransportObservations> weak_observations = observations;
        server->bind_upgrade([weak_observations](const std::shared_ptr<asio2::ws_session>& session) {
            if (const auto state = weak_observations.lock()) {
                std::lock_guard lock(state->mutex);
                state->target = std::string(session->get_upgrade_request().target());
            }
        });
        server->bind_recv([weak_observations](const auto&, std::string_view payload) {
            Message message{};
            if (!message.ParseFromString(std::string(payload)) || message.type() != kHeartBeat)
                return;
            if (const auto state = weak_observations.lock()) {
                std::lock_guard lock(state->mutex);
                state->device = message.device_id();
                state->stream = message.stream_id();
            }
        });
        return server->start("127.0.0.1", 0);
    }

    bool StartClient() {
        const std::weak_ptr<TransportObservations> weak_observations = observations;
        client->SetOnConnectCallback([weak_observations] {
            if (const auto state = weak_observations.lock())
                state->ready = true;
        });
        client->Start();
        return WaitFor([state = observations] {
            std::lock_guard lock(state->mutex);
            return state->ready.load() && !state->target.empty();
        });
    }
};

TEST(SdkConnectionParams, DefaultsDoNotEnableMediaOrContainAuthorization) {
    const SdkConnectionParams params{};
    EXPECT_EQ(params.media_transport_, SdkMediaTransport::kUdp);
    EXPECT_FALSE(params.ssl_);
    EXPECT_FALSE(params.enable_audio_);
    EXPECT_FALSE(params.enable_video_);
    EXPECT_FALSE(params.file_transfer_only_);
    EXPECT_EQ(params.port_, 0);
    EXPECT_EQ(params.udp_port_, 20371);
    EXPECT_TRUE(params.connection_ticket_.empty());
    EXPECT_TRUE(params.connection_nonce_.empty());
    EXPECT_TRUE(params.connection_instance_id_.empty());
    EXPECT_TRUE(params.udp_media_association_.empty());
}

TEST(SdkConnectionParams, SnapshotPreservesEndpointAuthorizationAndIdentityAfterCallerMutation) {
    TransportHarness harness{};
    ASSERT_TRUE(harness.StartServer());
    SdkConnectionParams params{
        .file_transfer_only_ = true,
        .ip_ = "127.0.0.1",
        .port_ = harness.server->listen_port(),
        .ft_path_ = "/file/transfer?original=1",
        .device_id_ = "snapshot-device",
        .stream_id_ = "snapshot-stream",
        .connection_ticket_ = "test-ticket",
        .connection_nonce_ = "test-nonce",
        .connection_instance_id_ = "test-instance",
    };
    harness.client = std::make_shared<NetClient>(params, harness.notifier);
    params = {};
    ASSERT_TRUE(harness.StartClient());
    harness.notifier->SendAppMessage(SdkMsgTimer1000{});
    ASSERT_TRUE(WaitFor([state = harness.observations] {
        std::lock_guard lock(state->mutex);
        return state->device == "snapshot-device" && state->stream == "snapshot-stream";
    }));
    {
        std::lock_guard lock(harness.observations->mutex);
        EXPECT_EQ(harness.observations->target,
                  "/file/transfer?original=1&ticket=test-ticket&client_nonce=test-nonce&instance_id=test-instance&file_only=1");
    }
    const std::weak_ptr<NetClient> weak_client = harness.client;
    harness.client->Exit();
    harness.client.reset();
    harness.notifier->SendAppMessage(SdkMsgTimer1000{});
    EXPECT_TRUE(WaitFor([weak_client] { return weak_client.expired(); }));
}

TEST(SdkConnectionParams, EmptyAssociationIsGeneratedPerClientWithoutMutatingCaller) {
    std::string previous_target{};
    for (int iteration{0}; iteration < 3; ++iteration) {
        TransportHarness harness{};
        ASSERT_TRUE(harness.StartServer());
        const SdkConnectionParams params{
            .ip_ = "127.0.0.1",
            .port_ = harness.server->listen_port(),
            .media_path_ = "/media?original=1",
            .stream_id_ = "ip-direct:test",
            .connection_nonce_ = "test-direct-nonce",
        };
        harness.client = std::make_shared<NetClient>(params, harness.notifier);
        ASSERT_TRUE(harness.StartClient());
        EXPECT_TRUE(params.udp_media_association_.empty());
        std::lock_guard lock(harness.observations->mutex);
        const auto& target = harness.observations->target;
        EXPECT_TRUE(target.starts_with("/media?original=1&udp_media=1&udp_media_association="));
        EXPECT_TRUE(target.ends_with("&client_nonce=test-direct-nonce"));
        EXPECT_NE(target, previous_target);
        previous_target = target;
    }
}

TEST(SdkConnectionParams, TcpSelectionPreservesAuthorizationWhenOnlyCachedUdpQueryWasPresent) {
    TransportHarness harness{};
    ASSERT_TRUE(harness.StartServer());
    const SdkConnectionParams params{
        .media_transport_ = SdkMediaTransport::kWebSocket,
        .ip_ = "127.0.0.1",
        .port_ = harness.server->listen_port(),
        .media_path_ = "/media?udp_media=1&udp_media_association=old",
        .connection_ticket_ = "test-ticket",
        .connection_nonce_ = "test-nonce",
        .connection_instance_id_ = "test-instance",
    };
    harness.client = std::make_shared<NetClient>(params, harness.notifier);
    ASSERT_TRUE(harness.StartClient());
    std::lock_guard lock(harness.observations->mutex);
    EXPECT_EQ(harness.observations->target, "/media?ticket=test-ticket&client_nonce=test-nonce&instance_id=test-instance");
}

TEST(SdkConnectionParams, ExplicitAssociationIsPreservedAfterSourceDestruction) {
    TransportHarness harness{};
    ASSERT_TRUE(harness.StartServer());
    harness.client = std::make_shared<NetClient>(
        SdkConnectionParams{
            .ip_ = "127.0.0.1",
            .port_ = harness.server->listen_port(),
            .media_path_ = "/media?original=1",
            .udp_media_association_ = "test-association",
        },
        harness.notifier);
    ASSERT_TRUE(harness.StartClient());
    std::lock_guard lock(harness.observations->mutex);
    EXPECT_EQ(harness.observations->target, "/media?original=1&udp_media=1&udp_media_association=test-association");
}

} // namespace
} // namespace px
