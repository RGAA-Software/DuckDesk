#include "sdk_net_client.h"
#include "sdk_messages.h"
#include "px_common/data.h"
#include "px_common/message_notifier.h"

#include <asio2/websocket/ws_server.hpp>
#include <asio2/udp/udp_server.hpp>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <thread>

namespace px {
namespace {
using namespace std::chrono_literals;

bool Await(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate() && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(10ms);
    return predicate();
}

struct Observations final {
    std::atomic_int configurations{0};
    std::atomic_int connections{0};
    std::atomic_int failures{0};
    std::atomic_int disconnected{0};
    std::atomic_int file_messages{0};
    std::atomic_int control_messages{0};
    std::atomic_int media_deliveries{0};
    std::atomic_bool fallback_requested{false};
    std::atomic_bool callback_finished{false};
    std::atomic_bool udp_requested{false};
    std::atomic_int media_acks{0};
    std::atomic_int udp_packets{0};
};

struct Session final {
    std::shared_ptr<MessageNotifier> notifier{std::make_shared<MessageNotifier>()};
    std::shared_ptr<asio2::ws_server> server{std::make_shared<asio2::ws_server>()};
    std::shared_ptr<asio2::udp_server> blackhole{std::make_shared<asio2::udp_server>()};
    std::shared_ptr<Observations> observations{std::make_shared<Observations>()};
    std::shared_ptr<NetClient> client{};

    ~Session() {
        if (client)
            client->Exit();
        server->stop();
        blackhole->stop();
        notifier->Stop(MessageBusStopMode::kCancel);
    }

    bool Start(bool file_only = false, SdkMediaTransport transport = SdkMediaTransport::kUdp, bool stop_on_video = false) {
        const std::weak_ptr<Observations> weak_observations = observations;
        server->bind_accept([weak_observations](const std::shared_ptr<asio2::ws_session>& session) {
            session->ws_stream().binary(true);
            if (const auto state = weak_observations.lock())
                ++state->connections;
        });
        server->bind_upgrade([weak_observations](const std::shared_ptr<asio2::ws_session>& session) {
            if (const auto state = weak_observations.lock()) {
                state->udp_requested = session->get_upgrade_request().target().find("udp_media") != std::string_view::npos;
            }
            Message configuration{};
            configuration.set_type(kServerConfiguration);
            session->async_send(configuration.SerializeAsString());
        });
        server->bind_recv([weak_observations](const std::shared_ptr<asio2::ws_session>& session, std::string_view payload) {
            const auto state = weak_observations.lock();
            if (!state)
                return;
            if (payload == kWsUseWebSocketMediaSignal)
                state->fallback_requested = true;
            if (payload == "control-probe")
                ++state->control_messages;
            Message received{};
            if (received.ParseFromArray(payload.data(), static_cast<int>(payload.size())) && received.type() == kAck) {
                ++state->media_acks;
            }
            if (payload != "file-probe")
                return;
            ++state->file_messages;
            // A misconfigured host must not sneak WS audio/video into decoding or recording after UDP fails.
            for (const auto type : {kVideoFrame, kAudioFrame, kVoiceAudioFrame}) {
                Message media{};
                media.set_type(type);
                session->async_send(media.SerializeAsString());
            }
        });
        blackhole->bind_recv([weak_observations](const auto&, std::string_view) {
            if (const auto state = weak_observations.lock()) {
                ++state->udp_packets;
            }
        });
        if (!server->start("127.0.0.1", 0) || !blackhole->start("127.0.0.1", 0))
            return false;
        const SdkConnectionParams params{
            .media_transport_ = transport,
            .enable_audio_ = !file_only,
            .enable_video_ = !file_only,
            .file_transfer_only_ = file_only,
            .ip_ = "127.0.0.1",
            .port_ = server->listen_port(),
            .udp_port_ = blackhole->listen_port(),
            .media_path_ = "/media?udp_media=1&test=1&udp_media_association=cached",
            .ft_path_ = "/file/transfer",
            .device_id_ = "client_test",
            .stream_id_ = "udp-failure-test",
        };
        client = std::make_shared<NetClient>(params, notifier);
        client->SetOnDisconnectedCallback([weak_observations]() {
            if (const auto state = weak_observations.lock())
                ++state->disconnected;
        });
        client->SetOnServerConfigurationCallback([weak_observations](const auto&) {
            if (const auto state = weak_observations.lock())
                ++state->configurations;
        });
        const std::weak_ptr<NetClient> weak_client = client;
        client->SetOnRawMessageCallback([weak_observations, weak_client, stop_on_video](const std::shared_ptr<Message>& message) {
            if (const auto state = weak_observations.lock();
                state && (message->type() == kVideoFrame || message->type() == kAudioFrame || message->type() == kVoiceAudioFrame)) {
                ++state->media_deliveries;
                if (stop_on_video && message->type() == kVideoFrame) {
                    if (const auto active_client = weak_client.lock()) {
                        active_client->Exit();
                    }
                    state->callback_finished = true;
                }
            }
        });
        client->SetOnVideoFrameMsgCallback([weak_observations](const auto&) {
            if (const auto state = weak_observations.lock())
                ++state->media_deliveries;
        });
        client->SetOnAudioFrameMsgCallback([weak_observations](const auto&) {
            if (const auto state = weak_observations.lock())
                ++state->media_deliveries;
        });
        client->Start();
        return Await([state = observations]() { return state->configurations > 0; });
    }

    void ExpireProbe() {
        std::this_thread::sleep_for(4200ms);
        notifier->SendAppMessage(SdkMsgTimer1000{});
    }
};

TEST(WebSocketMedia, ExplicitTcpDeliversMediaAndAcksWithoutUdpOrFallback) {
    for (int iteration{0}; iteration < 3; ++iteration) {
        Session session{};
        ASSERT_TRUE(session.Start(false, SdkMediaTransport::kWebSocket));
        session.client->Start();
        EXPECT_FALSE(session.observations->udp_requested);
        session.client->PostMediaMessage(Data::From("control-probe"));
        static_cast<void>(session.client->PostFileTransferMessage(Data::From("file-probe")));
        ASSERT_TRUE(Await([state = session.observations]() { return state->media_deliveries == 4 && state->media_acks >= 4; }));
        EXPECT_EQ(session.observations->control_messages, 1);
        EXPECT_EQ(session.observations->connections, 1);
        EXPECT_FALSE(session.observations->fallback_requested);
        EXPECT_EQ(session.observations->udp_packets, 0);
        session.client->Exit();
        session.client->Exit();
        session.client->Start();
        EXPECT_EQ(session.observations->connections, 1);
    }
}

TEST(WebSocketMedia, StopFromMediaCallbackRejectsQueuedFramesAndReleasesOwner) {
    Session session{};
    ASSERT_TRUE(session.Start(false, SdkMediaTransport::kWebSocket, true));
    const std::weak_ptr<NetClient> weak_client = session.client;
    static_cast<void>(session.client->PostFileTransferMessage(Data::From("file-probe")));
    ASSERT_TRUE(Await([state = session.observations]() { return state->callback_finished.load(); }));
    session.client.reset();
    EXPECT_TRUE(Await([weak_client]() { return weak_client.expired(); }));
    EXPECT_EQ(session.observations->media_deliveries, 1);
    EXPECT_EQ(session.observations->udp_packets, 0);
}

TEST(WebSocketMedia, TcpDoesNotStartUdpProbeWatchdog) {
    Session session{};
    const auto listener = session.notifier->CreateListener(MessageExecutionLane::kControl);
    listener->Listen<SdkMsgUdpMediaUnavailable>([state = session.observations](const auto&) { ++state->failures; });
    ASSERT_TRUE(session.Start(false, SdkMediaTransport::kWebSocket));
    session.ExpireProbe();
    session.client->PostMediaMessage(Data::From("control-probe"));
    ASSERT_TRUE(Await([state = session.observations]() { return state->control_messages == 1; }));
    EXPECT_EQ(session.observations->failures, 0);
    EXPECT_EQ(session.observations->udp_packets, 0);
}

TEST(UdpMediaFailure, FailedProbeKeepsControlAndFileAndRejectsWebSocketMedia) {
    Session session{};
    const auto listener = session.notifier->CreateListener(MessageExecutionLane::kControl);
    listener->Listen<SdkMsgUdpMediaUnavailable>([state = session.observations](const SdkMsgUdpMediaUnavailable& event) {
        EXPECT_EQ(event.reason, UdpMediaFailure::kProbeTimeout);
        ++state->failures;
    });
    ASSERT_TRUE(session.Start());
    session.client->Start();
    EXPECT_EQ(session.observations->configurations, 1);
    session.ExpireProbe();
    ASSERT_TRUE(Await([state = session.observations]() { return state->failures == 1; }));
    session.client->PostMediaMessage(Data::From("control-probe"));
    static_cast<void>(session.client->PostFileTransferMessage(Data::From("file-probe")));
    ASSERT_TRUE(Await([state = session.observations]() { return state->file_messages == 1 && state->control_messages == 1; }));
    session.notifier->SendAppMessage(SdkMsgTimer1000{});
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(session.observations->failures, 1);
    EXPECT_EQ(session.observations->disconnected, 0);
    EXPECT_EQ(session.observations->media_deliveries, 0);
    EXPECT_FALSE(session.observations->fallback_requested);
}

TEST(UdpMediaFailure, FileOnlySessionDoesNotRequireUdpMedia) {
    Session session{};
    const auto listener = session.notifier->CreateListener(MessageExecutionLane::kControl);
    listener->Listen<SdkMsgUdpMediaUnavailable>([state = session.observations](const auto&) { ++state->failures; });
    ASSERT_TRUE(session.Start(true));
    session.notifier->SendAppMessage(SdkMsgTimer1000{});
    static_cast<void>(session.client->PostFileTransferMessage(Data::From("file-probe")));
    ASSERT_TRUE(Await([state = session.observations]() { return state->file_messages == 1; }));
    std::this_thread::sleep_for(100ms);
    EXPECT_EQ(session.observations->failures, 0);
    EXPECT_FALSE(session.observations->fallback_requested);
    EXPECT_EQ(session.observations->media_deliveries, 0);
}

TEST(UdpMediaFailure, FileOnlyRepeatedStartAndStopNeverReopensTheSession) {
    for (int iteration{0}; iteration < 3; ++iteration) {
        Session session{};
        ASSERT_TRUE(session.Start(true));
        session.client->Start();
        session.client->Start();
        std::this_thread::sleep_for(50ms);
        EXPECT_EQ(session.observations->connections, 1);
        session.client->Exit();
        session.client->Exit();
        session.client->Start();
        EXPECT_EQ(session.client->PostFileTransferMessage(Data::From("file-probe")).status(), FileTransferSendStatus::kDisconnected);
        session.notifier->SendAppMessage(SdkMsgTimer1000{});
        std::this_thread::sleep_for(50ms);
        EXPECT_EQ(session.observations->connections, 1);
        EXPECT_EQ(session.observations->file_messages, 0);
    }
}

TEST(UdpMediaFailure, FailureCallbackCanUnregisterAndStopWithoutResurrectingTheSession) {
    Session session{};
    ASSERT_TRUE(session.Start());
    const auto listener = session.notifier->CreateListener(MessageExecutionLane::kControl);
    const std::weak_ptr<MessageListener> weak_listener = listener;
    const std::weak_ptr<NetClient> weak_client = session.client;
    listener->Listen<SdkMsgUdpMediaUnavailable>([weak_listener, weak_client, state = session.observations](const auto&) {
        ++state->failures;
        if (const auto current_listener = weak_listener.lock())
            current_listener->UnListenAll();
        if (const auto client = weak_client.lock())
            client->Exit();
        state->callback_finished = true;
    });
    session.ExpireProbe();
    ASSERT_TRUE(Await([state = session.observations]() { return state->callback_finished.load(); }));
    session.client.reset();
    session.notifier->SendAppMessage(SdkMsgTimer1000{});
    session.notifier->SendAppMessage(SdkMsgUdpMediaUnavailable{});
    std::this_thread::sleep_for(100ms);
    EXPECT_TRUE(weak_client.expired());
    EXPECT_EQ(session.observations->failures, 1);
}

} // namespace
} // namespace px
