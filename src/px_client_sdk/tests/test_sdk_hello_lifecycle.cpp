#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include <asio2/websocket/ws_server.hpp>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>
#include <thread>

#include "px_common/message_notifier.h"
#include "px_common/scope_exit.h"
#include "sdk_messages.h"
#include "sdk_params.h"
#include "sdk_video_decoder_factory.h"
#include "thunder_sdk.h"

namespace px {
namespace {

bool WaitForHelloCondition(const std::function<bool()>& predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    return predicate();
}

class HelloDecoderFactory final : public VideoDecoderFactory {
public:
    VideoDecoderCreation Create(const std::shared_ptr<ThunderSdk>&, const VideoFrame&, bool) override { return {}; }
    bool SupportsMultipleStreams() const noexcept override { return false; }
};

struct HelloFixture final {
    std::shared_ptr<MessageNotifier> notifier{std::make_shared<MessageNotifier>()};
    std::shared_ptr<ThunderSdk> sdk{ThunderSdk::Make(notifier)};
    std::shared_ptr<asio2::ws_server> server{std::make_shared<asio2::ws_server>()};
    std::shared_ptr<std::atomic_int> helloCount{std::make_shared<std::atomic_int>(0)};
    std::shared_ptr<std::atomic_bool> disconnected{std::make_shared<std::atomic_bool>(false)};
    std::shared_ptr<MessageListener> listener{notifier->CreateListener(MessageExecutionLane::kState)};

    ~HelloFixture() {
        sdk->Exit();
        server->stop();
        notifier->Stop(MessageBusStopMode::kCancel);
    }

    bool Initialize(bool startServer, bool failBeforeTransportReady = false) {
        const auto params = std::make_shared<ThunderSdkParams>();
        params->ip_ = "127.0.0.1";
        params->media_transport_ = SdkMediaTransport::kWebSocket;
        params->enable_video_ = true;
        params->media_path_ = "/hello-lifecycle-test";
        if (failBeforeTransportReady) params->iroh_.emplace();
        if (startServer) {
            server->bind_recv([count = helloCount](const std::shared_ptr<asio2::ws_session>&, std::string_view payload) {
                Message envelope{};
                if (envelope.ParseFromArray(payload.data(), static_cast<int>(payload.size())) && envelope.type() == kHello) ++*count;
            });
            if (!server->start("127.0.0.1", 0)) return false;
            params->port_ = server->listen_port();
        }
        listener->Listen<SdkMsgNetworkDisConnected>([observed = disconnected](const auto&) { observed->store(true); });
        return sdk->Init(params, std::make_shared<HelloDecoderFactory>());
    }

    bool Tick() {
        notifier->SendAppMessage(SdkMsgTimer1000{});
        return notifier->FlushForTest();
    }
};

TEST(SdkHelloLifecycle, UnreadyOrFailedDialDoesNotAttemptHelloEnqueue) {
    std::ostringstream diagnostics{};
    const auto previousLogger = spdlog::default_logger();
    const auto restoreLogger = PxScopeExit{[previousLogger] { spdlog::set_default_logger(previousLogger); }};
    const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(diagnostics);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("unready-hello", sink));
    {
        HelloFixture fixture{};
        ASSERT_TRUE(fixture.Initialize(false, true));
        ASSERT_TRUE(fixture.Tick());
        fixture.sdk->Start();
        ASSERT_TRUE(WaitForHelloCondition([observed = fixture.disconnected] { return observed->load(); }));
        for (int timerIndex{}; timerIndex < 4; ++timerIndex) ASSERT_TRUE(fixture.Tick());
    }
    EXPECT_EQ(diagnostics.str().find("event=session.hello"), std::string::npos);
    EXPECT_NE(diagnostics.str().find("code=IROH_ENDPOINT_MISSING recoverable=false action=end_session"), std::string::npos);
}

TEST(SdkHelloLifecycle, ConnectedHelloRetriesStopAfterDisconnect) {
    std::ostringstream diagnostics{};
    const auto previousLogger = spdlog::default_logger();
    const auto restoreLogger = PxScopeExit{[previousLogger] { spdlog::set_default_logger(previousLogger); }};
    const auto sink = std::make_shared<spdlog::sinks::ostream_sink_mt>(diagnostics);
    spdlog::set_default_logger(std::make_shared<spdlog::logger>("connected-hello", sink));
    {
        HelloFixture fixture{};
        ASSERT_TRUE(fixture.Initialize(true));
        fixture.sdk->Start();
        ASSERT_TRUE(WaitForHelloCondition([count = fixture.helloCount] { return count->load() >= 1; }));
        ASSERT_TRUE(fixture.Tick());
        ASSERT_TRUE(WaitForHelloCondition([count = fixture.helloCount] { return count->load() >= 2; }));
        fixture.server->stop();
        ASSERT_TRUE(WaitForHelloCondition([observed = fixture.disconnected] { return observed->load(); }));
        for (int timerIndex{}; timerIndex < 4; ++timerIndex) ASSERT_TRUE(fixture.Tick());
    }
    EXPECT_EQ(diagnostics.str().find("event=session.hello"), std::string::npos);
}

}  // namespace
}  // namespace px
