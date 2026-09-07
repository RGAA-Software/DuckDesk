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
    std::atomic_int failures{0};
    std::atomic_int disconnected{0};
    std::atomic_int file_messages{0};
    std::atomic_int control_messages{0};
    std::atomic_int media_deliveries{0};
    std::atomic_bool fallback_requested{false};
    std::atomic_bool callback_finished{false};
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

    bool Start(bool file_only = false) {
        const std::weak_ptr<Observations> weak_observations = observations;
        server->bind_accept([](const std::shared_ptr<asio2::ws_session>& session) { session->ws_stream().binary(true); });
        server->bind_upgrade([](const std::shared_ptr<asio2::ws_session>& session) {
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
            if (payload != "file-probe")
                return;
            ++state->file_messages;
            // A misconfigured host must not sneak WS audio/video into decoding or recording after UDP fails.
            for (const auto type : {kVideoFrame, kAudioFrame}) {
                Message media{};
                media.set_type(type);
                session->async_send(media.SerializeAsString());
            }
        });
        if (!server->start("127.0.0.1", 0) || !blackhole->start("127.0.0.1", 0))
            return false;
        const auto params = std::make_shared<ThunderSdkParams>();
        params->ip_ = "127.0.0.1";
        params->port_ = server->listen_port();
        params->udp_port_ = blackhole->listen_port();
        params->enable_video_ = !file_only;
        params->file_transfer_only_ = file_only;
        params->nt_type_ = ClientNetworkType::kUdpDirect;
        params->client_type_ = ClientType::kUnknown;
        params->stream_id_ = "udp-failure-test";
        client = std::make_shared<NetClient>(params, notifier, params->ip_, params->port_, "/media?udp_media=1", "/file/transfer", params->nt_type_,
                                             "client_test", "server_test", "file_client", "file_server", params->stream_id_);
        client->SetOnDisconnectedCallback([weak_observations]() {
            if (const auto state = weak_observations.lock())
                ++state->disconnected;
        });
        client->SetOnServerConfigurationCallback([weak_observations](const auto&) {
            if (const auto state = weak_observations.lock())
                ++state->configurations;
        });
        client->SetOnRawMessageCallback([weak_observations](const std::shared_ptr<Message>& message) {
            if (const auto state = weak_observations.lock(); state && (message->type() == kVideoFrame || message->type() == kAudioFrame)) {
                ++state->media_deliveries;
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
