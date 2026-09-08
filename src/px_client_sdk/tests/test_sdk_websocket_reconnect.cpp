#include <atomic>
#include <chrono>
#include <functional>
#include <fstream>
#include <iterator>
#include <memory>
#include <thread>

#include <Windows.h>
#include <asio2/websocket/ws_server.hpp>
#include <asio2/websocket/wss_server.hpp>
#include <gtest/gtest.h>

#include "connection/sdk_websocket_reconnect.h"
#include "connection/ws_connection.h"
#include "connection/wss_connection.h"
#include "px_common/message_notifier.h"
#include "sdk_messages.h"

namespace px {
namespace {

using namespace std::chrono_literals;

bool WaitUntil(const std::function<bool()>& predicate, const std::chrono::steady_clock::duration timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(10ms);
    }
    return predicate();
}

std::shared_ptr<WsConnection> MakeConnection(
    const std::shared_ptr<MessageNotifier>& notifier,
    const int port) {
    return std::make_shared<WsConnection>(notifier, "127.0.0.1", port, "/sdk-reconnect-test");
}

std::shared_ptr<WssConnection> MakeSecureConnection(
    const std::shared_ptr<MessageNotifier>& notifier,
    const int port) {
    return std::make_shared<WssConnection>(notifier, "127.0.0.1", port, "/sdk-secure-reconnect-test");
}

std::shared_ptr<asio2::wss_server> MakeSecureServer() {
    std::ifstream input(PX_TEST_TLS_PEM, std::ios::binary);
    const std::string pem{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    constexpr std::string_view key_begin = "-----BEGIN RSA PRIVATE KEY-----";
    constexpr std::string_view key_end = "-----END RSA PRIVATE KEY-----";
    constexpr std::string_view cert_begin = "-----BEGIN CERTIFICATE-----";
    constexpr std::string_view cert_end = "-----END CERTIFICATE-----";
    const auto key_first = pem.find(key_begin);
    const auto key_last = pem.find(key_end);
    const auto cert_first = pem.find(cert_begin);
    const auto cert_last = pem.find(cert_end, cert_first);
    if (key_first == std::string::npos || key_last == std::string::npos || cert_first == std::string::npos
        || cert_last == std::string::npos) {
        return {};
    }
    const auto key = pem.substr(key_first, key_last + key_end.size() - key_first);
    const auto certificate = pem.substr(cert_first, cert_last + cert_end.size() - cert_first);
    const auto server = std::make_shared<asio2::wss_server>();
    server->set_cert_buffer({}, certificate, key, {});
    server->set_verify_mode(asio::ssl::verify_none);
    return server;
}

TEST(SdkWebSocketReconnect, InitiallyUnavailableServerIsRetriedUntilItStarts) {
    const auto port = 50000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto connection = MakeConnection(notifier, port);
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    connection->RegisterOnConnectedCallback([connected] {
        connected->fetch_add(1, std::memory_order_acq_rel);
    });
    connection->Start();

    ASSERT_TRUE(WaitUntil([connection] { return connection->ConnectionGeneration() >= 2; }, 6s));
    const auto generation_before_server_start = connection->ConnectionGeneration();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    ASSERT_TRUE(WaitUntil([connection, generation_before_server_start] {
        return connection->IsAlive() && connection->ConnectionGeneration() >= generation_before_server_start;
    }, 10s));
    EXPECT_EQ(connected->load(std::memory_order_acquire), 1U);

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
    EXPECT_FALSE(connection->IsAlive());
}

TEST(SdkWebSocketReconnect, RealServerRestartsAdvanceEveryGeneration) {
    const auto port = 55000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto connection = MakeConnection(notifier, port);
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    const auto disconnected = std::make_shared<std::atomic_uint32_t>(0);
    connection->RegisterOnConnectedCallback([connected] {
        connected->fetch_add(1, std::memory_order_acq_rel);
    });
    connection->RegisterOnDisConnectedCallback([disconnected] {
        disconnected->fetch_add(1, std::memory_order_acq_rel);
    });
    connection->Start();
    ASSERT_TRUE(WaitUntil([connection] { return connection->IsAlive(); }, 5s));
    auto previous_generation = connection->ConnectionGeneration();

    for (int cycle = 0; cycle < 3; ++cycle) {
        server->stop();
        ASSERT_TRUE(WaitUntil([connection] { return !connection->IsAlive(); }, 3s)) << "cycle=" << cycle;
        ASSERT_TRUE(server->start("127.0.0.1", port)) << "cycle=" << cycle;
        ASSERT_TRUE(WaitUntil([connection, previous_generation] {
            return connection->IsAlive() && connection->ConnectionGeneration() > previous_generation;
        }, 10s)) << "cycle=" << cycle;
        previous_generation = connection->ConnectionGeneration();
    }
    EXPECT_EQ(connected->load(std::memory_order_acquire), 4U);
    EXPECT_EQ(disconnected->load(std::memory_order_acquire), 3U);

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, SecureServerRestartAdvancesGeneration) {
    const auto port = 53000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto server = MakeSecureServer();
    ASSERT_TRUE(server);
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto connection = MakeSecureConnection(notifier, port);
    connection->Start();
    ASSERT_TRUE(WaitUntil([connection] { return connection->IsAlive(); }, 10s));
    const auto previous_generation = connection->ConnectionGeneration();

    server->stop();
    ASSERT_TRUE(WaitUntil([connection] { return !connection->IsAlive(); }, 3s));
    ASSERT_TRUE(server->start("127.0.0.1", port));
    ASSERT_TRUE(WaitUntil([connection, previous_generation] {
        return connection->IsAlive() && connection->ConnectionGeneration() > previous_generation;
    }, 10s));

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, RepeatedStartStopLeavesNoLiveAdapter) {
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto connection = MakeConnection(notifier, 9);
    for (int cycle = 0; cycle < 10; ++cycle) {
        connection->Start();
        connection->Stop();
        EXPECT_FALSE(connection->IsAlive()) << "cycle=" << cycle;
    }
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, StopFromReadyCallbackDrainsAndAllowsRestart) {
    const auto port = 52000 + static_cast<int>(GetCurrentProcessId() % 5000);
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto server = std::make_shared<asio2::ws_server>();
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto connection = MakeConnection(notifier, port);
    const std::weak_ptr<WsConnection> weak_connection = connection;
    const auto connected = std::make_shared<std::atomic_uint32_t>(0);
    connection->RegisterOnConnectedCallback([weak_connection, connected] {
        if (connected->fetch_add(1, std::memory_order_acq_rel) == 0) {
            if (const auto current = weak_connection.lock()) {
                current->Stop();
            }
        }
    });

    connection->Start();
    ASSERT_TRUE(WaitUntil([connected] { return connected->load(std::memory_order_acquire) == 1; }, 5s));
    ASSERT_TRUE(WaitUntil([connection, connected] {
        connection->Start();
        return connected->load(std::memory_order_acquire) >= 2 && connection->IsAlive();
    }, 10s));

    connection->Stop();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, IngressRejectsStaleAndStoppedAttemptGenerations) {
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto runtime = notifier->GetAsyncRuntime();
    const auto scope = PxAsyncScope::Create(runtime, PxAsyncLane::kState);
    const auto supervisor = PxReconnectSupervisor::Create(
        runtime, {.component = "sdk-ingress-test", .backoff = {.initial_delay = 1ms, .maximum_delay = 2ms, .jitter_ratio = 0.0}});
    ASSERT_TRUE(supervisor);
    EXPECT_FALSE(CanDeliverSdkWebSocketMessage({}, 1));
    EXPECT_FALSE(CanDeliverSdkWebSocketMessage(supervisor, 0));
    PxReconnectSupervisorHooks hooks{
        .start_attempt =
            [weak_supervisor = std::weak_ptr<PxReconnectSupervisor>(supervisor)](std::uint64_t generation) {
                if (const auto current = weak_supervisor.lock()) {
                    EXPECT_FALSE(CanDeliverSdkWebSocketMessage(current, generation));
                    EXPECT_TRUE(current->MarkReady(generation));
                }
                return PxResult<void>::Success();
            },
        .stop_attempt = [](std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<void>> { co_return PxResult<void>::Success(); },
    };
    ASSERT_TRUE(scope->Spawn("sdk-ingress-generation",
                             [supervisor, hooks = std::move(hooks)]() mutable { return PxReconnectSupervisor::Run(supervisor, std::move(hooks)); }));
    ASSERT_TRUE(WaitUntil([supervisor] { return supervisor->IsReady(); }, 1s));
    const auto previous = supervisor->Generation();
    EXPECT_TRUE(CanDeliverSdkWebSocketMessage(supervisor, previous));
    EXPECT_TRUE(
        supervisor->MarkDisconnected(previous, MakePxAsyncError(PxAsyncErrorCode::kServiceNotConnected, "test.disconnect", "test disconnect", true)));
    EXPECT_FALSE(CanDeliverSdkWebSocketMessage(supervisor, previous));
    ASSERT_TRUE(WaitUntil([supervisor, previous] { return supervisor->Generation() > previous && supervisor->IsReady(); }, 1s));
    EXPECT_FALSE(CanDeliverSdkWebSocketMessage(supervisor, previous));
    EXPECT_TRUE(CanDeliverSdkWebSocketMessage(supervisor, supervisor->Generation()));
    supervisor->Stop();
    EXPECT_FALSE(CanDeliverSdkWebSocketMessage(supervisor, supervisor->Generation()));
    scope->BeginStop();
    EXPECT_TRUE(scope->WaitFor(1s));
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, RealAdmissionRejectionStopsRetryAndLaterBusinessMessages) {
    const auto notifier = std::make_shared<MessageNotifier>();
    const auto server = std::make_shared<asio2::ws_server>();
    const auto accepts = std::make_shared<std::atomic_int>(0);
    server->bind_upgrade([accepts](const std::shared_ptr<asio2::ws_session>& session) {
        if (++*accepts == 1) {
            session->async_send(std::string(kWsSessionOccupiedSignal));
            session->async_send(std::string("business-after-rejection"));
        } else {
            session->async_send(std::string("fresh-session-message"));
        }
    });
    const auto port = 51000 + static_cast<int>(GetCurrentProcessId() % 1000);
    ASSERT_TRUE(server->start("127.0.0.1", port));
    const auto rejection = std::make_shared<std::atomic_bool>(false);
    const auto listener = notifier->CreateListener();
    listener->Listen<SdkMsgWsConnectionRejected>([rejection](const SdkMsgWsConnectionRejected& event) {
        EXPECT_EQ(event.rejection_, WsControlRejection::kOccupied);
        *rejection = true;
    });
    const auto messages = std::make_shared<std::atomic_int>(0);
    const auto connection = MakeConnection(notifier, port);
    connection->RegisterOnMessageCallback([messages](std::shared_ptr<Data>) { ++*messages; });
    connection->Start();
    ASSERT_TRUE(WaitUntil([rejection] { return rejection->load(); }, 3s));
    std::this_thread::sleep_for(1200ms);
    EXPECT_EQ(accepts->load(), 1);
    EXPECT_EQ(messages->load(), 0);
    EXPECT_FALSE(connection->IsAlive());
    connection->Stop();
    const auto fresh = MakeConnection(notifier, port);
    fresh->RegisterOnMessageCallback([messages](std::shared_ptr<Data>) { ++*messages; });
    fresh->Start();
    ASSERT_TRUE(WaitUntil([messages] { return messages->load() == 1; }, 3s));
    EXPECT_EQ(accepts->load(), 2);
    EXPECT_TRUE(fresh->IsAlive());
    fresh->Stop();
    listener->UnListenAll();
    server->stop();
    notifier->Stop(MessageBusStopMode::kCancel);
}

TEST(SdkWebSocketReconnect, SessionRejectionIsTerminal) {
    for (const auto rejection : {
             WsControlRejection::kAuthorization,
             WsControlRejection::kOccupied,
             WsControlRejection::kSessionPolicy}) {
        const auto error = MakeSdkWebSocketRejectionError(rejection);
        EXPECT_FALSE(error.retryable);
        EXPECT_EQ(error.StableCode(), "SDK_WEBSOCKET_SESSION_REJECTED");
    }
}

} // namespace
} // namespace px
