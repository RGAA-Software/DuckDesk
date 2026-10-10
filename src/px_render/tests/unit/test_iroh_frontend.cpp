#include <gtest/gtest.h>

#include <asio2/tcp/tcp_server.hpp>
#include <condition_variable>
#include <filesystem>
#include <future>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

#include "connection/iroh_connection.h"
#include "px_common/message_notifier.h"
#include "px_common/scope_exit.h"
#include "px_message.pb.h"
#include "px_render/modules/module_ids.h"
#include "px_render/network/iroh/iroh_session.h"
#include "px_render/network/iroh/iroh_transport.h"
#include "px_render/network/ws/ws_transport.h"
#include "px_transport/tests/endpoint_configuration.h"
#include "sdk_net_client.h"

namespace px {
namespace {

std::int64_t SystemMilliseconds() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

struct FrontendTestState final {
    std::shared_ptr<LogicalSessionRegistry> sessions{std::make_shared<LogicalSessionRegistry>(false, false, 10000)};
    std::mutex mutex{};
    std::condition_variable changed{};
    std::size_t connected{};
    std::size_t disconnected{};
    std::size_t released_quotas{};
    std::vector<std::uint64_t> invalid_references{};
    std::size_t key_frame_requests{};
    std::vector<EncodedVideoDelivery> videos{};
    std::vector<std::shared_ptr<Message>> audio{};
    std::shared_ptr<AcceptedIrohFrontend> accepted{};
    std::shared_ptr<IrohSession> routed{};
    std::vector<std::shared_ptr<NetworkClientEvent>> messages{};
};

class IrohFrontendTest : public testing::Test {
protected:
    void SetUp() override {
        runtime_ = PxAsyncRuntime::Create();
        ASSERT_TRUE(runtime_->Start());
        services_ = std::make_shared<WsTransport>(runtime_);
        const auto temporary_path = std::filesystem::temp_directory_path() / "pixels-iroh-frontend-test";
        std::filesystem::create_directories(temporary_path);
        ASSERT_TRUE(services_->RenderModule::Start(
            {.async_runtime = runtime_, .instance_name = "iroh-test", .base_data_path = temporary_path.wstring(), .device_id = "iroh-test-device"}));
        services_->UpdateSettings({.device_safety_password = "test-password-digest"});
        services_->ConfigureDirectStreamAuthorizer(
            [state = state_](std::string, bool release, std::chrono::steady_clock::time_point) { return AuthorizeDirect(state, release); });
        services_->ConfigureLogicalLeaseRenewer(
            [state = state_](const LogicalSessionGrant& grant, std::int64_t now_ms) { return state->sessions->RenewLease(grant, now_ms); });
        events_ = [state = state_](const RenderEventEnvelope& event) {
            std::visit(
                [&state, &event](const auto& payload) {
                    using Payload = std::decay_t<decltype(*payload)>;
                    if constexpr (std::is_same_v<Payload, AdmitLogicalSessionEvent>) {
                        payload->callback_(
                            state->sessions->Bind(payload->grant_, payload->transport_, payload->binding_id_, false, SystemMilliseconds()));
                    } else if constexpr (std::is_same_v<Payload, CloseLogicalSessionBindingEvent>) {
                        static_cast<void>(state->sessions->CloseBinding(payload->logical_session_id_, payload->binding_id_, SystemMilliseconds()));
                    } else if constexpr (std::is_same_v<Payload, ClientConnectedEvent>) {
                        std::lock_guard lock(state->mutex);
                        EXPECT_EQ(event.source_id, kNetIrohTransportId);
                        ++state->connected;
                        state->changed.notify_all();
                    } else if constexpr (std::is_same_v<Payload, NetworkClientEvent>) {
                        EXPECT_EQ(event.source_id, kNetIrohTransportId);
                        std::lock_guard lock(state->mutex);
                        state->messages.push_back(payload);
                        state->changed.notify_all();
                    } else if constexpr (std::is_same_v<Payload, KeyFrameRequestEvent>) {
                        std::lock_guard lock(state->mutex);
                        ++state->key_frame_requests;
                        state->changed.notify_all();
                    } else if constexpr (std::is_same_v<Payload, ReferenceFrameInvalidationEvent>) {
                        std::lock_guard lock(state->mutex);
                        state->invalid_references.push_back(payload->invalid_frame_index_);
                        state->changed.notify_all();
                    } else if constexpr (std::is_same_v<Payload, ClientDisconnectedEvent>) {
                        std::lock_guard lock(state->mutex);
                        EXPECT_EQ(event.source_id, kNetIrohTransportId);
                        ++state->disconnected;
                        state->changed.notify_all();
                    }
                },
                event.payload);
        };
        services_->SetEventCallback(events_);
        server_ = std::make_shared<IrohServer>(
            services_, runtime_, false,
            [state = state_](AcceptedIrohFrontend accepted) {
                std::lock_guard lock(state->mutex);
                state->accepted = std::make_shared<AcceptedIrohFrontend>(std::move(accepted));
                return true;
            },
            events_);
        ASSERT_TRUE(server_->Start(transport::testing::EndpointConfiguration()));
        endpoint_ = transport::Endpoint::Bind(transport::testing::EndpointConfiguration(), 5000);
        ASSERT_TRUE(endpoint_);
    }

    void TearDown() override {
        if (server_) server_->Stop();
        if (endpoint_) endpoint_->Close();
        state_->accepted.reset();
        state_->routed.reset();
        if (services_) static_cast<void>(services_->Destroy());
        if (runtime_) {
            runtime_->RequestStop();
            runtime_->Join();
        }
    }

    static PxAwaitable<PxResult<std::uint32_t>> AuthorizeDirect(std::shared_ptr<FrontendTestState> state, bool release) {
        if (release) {
            std::lock_guard lock(state->mutex);
            ++state->released_quotas;
            state->changed.notify_all();
        }
        co_return PxResult<std::uint32_t>::Success(30000);
    }

    void RouteSessions(bool rdp = false, std::uint16_t proxy_port = 0) {
        server_->Stop();
        server_ = std::make_shared<IrohServer>(
            services_, runtime_, rdp,
            [state = state_, events = events_, executor = runtime_->Executor(PxAsyncLane::kWorker), proxy_port](AcceptedIrohFrontend accepted) {
                const auto session = std::make_shared<IrohSession>(std::move(accepted), events);
                if (!session->Start(executor, proxy_port)) return false;
                std::lock_guard lock(state->mutex);
                state->routed = session;
                return true;
            },
            events_);
        ASSERT_TRUE(server_->Start(transport::testing::EndpointConfiguration()));
    }

    std::shared_ptr<IrohSession> WaitForRoute() {
        std::unique_lock lock(state_->mutex);
        if (!state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->connected == 1; })) return {};
        return state_->routed;
    }

    std::shared_ptr<transport::Connection> Connect() {
        const auto address = server_->Address();
        if (!address) return {};
        return endpoint_->Connect(*address, 5000);
    }

    transport::FrontendParameters Parameters(const std::string& stream_id) {
        return {{"stream_id", stream_id}, {"client_nonce", "test-nonce"}, {"safety_pwd_md5", "test-password-digest"}};
    }

    RenderEventCallback events_{};
    std::shared_ptr<FrontendTestState> state_{std::make_shared<FrontendTestState>()};
    std::shared_ptr<PxAsyncRuntime> runtime_{};
    std::shared_ptr<WsTransport> services_{};
    std::shared_ptr<IrohServer> server_{};
    std::shared_ptr<transport::Endpoint> endpoint_{};
};

TEST_F(IrohFrontendTest, DesktopAcceptsExistingConsoleSessionWithoutDevicePassword) {
    services_->ConfigureFrontendAuthorizer(
        [](ConsoleFrontendAdmissionRequest request, std::chrono::steady_clock::time_point) -> PxAwaitable<PxResult<ConsoleFrontendGrant>> {
            co_return PxResult<ConsoleFrontendGrant>::Success({.session_id = request.session_id,
                                                               .revision = request.revision,
                                                               .target_kind = "desktop",
                                                               .device_id = "iroh-test-device",
                                                               .client_type = "panel",
                                                               .access_role = "controller",
                                                               .valid_for_ms = 30000});
        });
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection,
                                                {{"stream_id", "console-desktop"},
                                                 {"session_id", "console-desktop"},
                                                 {"session_revision", "1"},
                                                 {"frontend_token", "existing-console-authorization"}},
                                                10000);
    ASSERT_TRUE(opened);
    EXPECT_TRUE(opened->reply.accepted) << opened->reply.code;
    connection->Close();
}

TEST_F(IrohFrontendTest, SequencedBackpressureDoesNotRequestPrePacketizationRecovery) {
    server_->Stop();
    const auto module = std::make_shared<IrohTransport>(services_, runtime_);
    module->SetEventCallback(events_);
    ASSERT_TRUE(module->Start({.async_runtime = runtime_,
                               .instance_name = "iroh-busy-module-test",
                               .base_data_path = (std::filesystem::temp_directory_path() / "pixels-iroh-frontend-test").wstring(),
                               .iroh_endpoint_configuration = transport::testing::EndpointConfiguration(),
                               .device_id = "iroh-test-device"}));
    const auto address = module->Address();
    ASSERT_TRUE(address);
    const auto connection = endpoint_->Connect(*address, 10000);
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("busy-module-client"), 10000);
    ASSERT_TRUE(opened && opened->reply.accepted);
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->connected == 1; }));
    }
    EncodedVideoFrameEvent encoded{.type_ = EncodedVideoType::kH264,
                                   .data_ = Data::From(std::string(240000, 0x41)),
                                   .frame_width_ = 640,
                                   .frame_height_ = 480,
                                   .key_frame_ = true,
                                   .frame_index_ = 1};
    ASSERT_TRUE(module->SubmitVideo("application", encoded));
    encoded.data_ = Data::From(std::string(1024, 0x42));
    encoded.key_frame_ = false;
    // Offered faster than a single large frame can drain: busy is handled inside
    // the sequenced sender. False here would create another IDR for every drop.
    for (std::uint64_t frame_index{2}; frame_index < 102; ++frame_index) {
        encoded.frame_index_ = frame_index;
        ASSERT_TRUE(module->SubmitVideo("application", encoded));
    }
    connection->Close();
    module->Stop();
}

TEST_F(IrohFrontendTest, RenderModuleRoutesEncoderAndFileOutputToNetClient) {
    server_->Stop();
    const auto module = std::make_shared<IrohTransport>(services_, runtime_);
    // Total link selection includes audio, FEC and framing; do not spend the entire selection on video payload.
    EXPECT_LT(module->VideoEncodingBitrate(20'000'000) * 120 / 100, 19'000'000);
    EXPECT_GT(module->VideoEncodingBitrate(20'000'000), module->VideoEncodingBitrate(8'000'000));
    module->SetEventCallback(events_);
    ASSERT_TRUE(module->Start({.async_runtime = runtime_,
                               .instance_name = "iroh-module-test",
                               .base_data_path = (std::filesystem::temp_directory_path() / "pixels-iroh-frontend-test").wstring(),
                               .iroh_endpoint_configuration = transport::testing::EndpointConfiguration(),
                               .device_id = "iroh-test-device"}));
    const auto address = module->Address();
    ASSERT_TRUE(address);
    const auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{.runtime = runtime_});
    SdkConnectionParams parameters{};
    parameters.iroh_ = IrohDialParameters{
        .endpoint_address = *address, .endpoint_configuration = transport::testing::EndpointConfiguration(), .frontend = Parameters("module-client")};
    parameters.enable_video_ = true;
    parameters.stream_id_ = "module-client";
    const auto client = std::make_shared<NetClient>(std::move(parameters), notifier);
    const auto connected = std::make_shared<std::promise<void>>();
    auto ready = connected->get_future();
    const auto frame_received = std::make_shared<std::promise<std::string>>();
    auto received = frame_received->get_future();
    const auto file_received = std::make_shared<std::promise<std::string>>();
    auto received_file = file_received->get_future();
    client->SetOnConnectCallback([connected] { connected->set_value(); });
    client->SetOnVideoFrameMsgCallback(
        [frame_received](EncodedVideoDelivery delivery) { frame_received->set_value(delivery.message->video_frame().data()); });
    client->SetOnRawMessageCallback([file_received](std::shared_ptr<Message> message) {
        if (message->type() == kFileResponse) file_received->set_value(message->file_response().block().data());
    });
    client->Start();
    ASSERT_EQ(ready.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->connected == 1; }));
    }
    EXPECT_EQ(module->ConnectedClientCount(), 1);
    EXPECT_TRUE(module->HasVideoClient());
    const EncodedVideoFrameEvent encoded{.type_ = EncodedVideoType::kH264,
                                         .data_ = Data::From(std::string(9000, 0x41)),
                                         .frame_width_ = 640,
                                         .frame_height_ = 480,
                                         .key_frame_ = true,
                                         .frame_index_ = 1};
    ASSERT_TRUE(module->SubmitVideo("application", encoded));
    ASSERT_EQ(received.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(received.get(), std::string(9000, 0x41));
    Message file{};
    file.set_type(kFileResponse);
    file.mutable_file_response()->mutable_block()->set_data("module-file-payload");
    ASSERT_TRUE(module->SendFile("module-client", Data::From(file.SerializeAsString())).accepted());
    ASSERT_EQ(received_file.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(received_file.get(), "module-file-payload");
    module->UpdatePermissions("module-client", {"view", "audio"});
    EXPECT_EQ(module->SendFile("module-client", Data::From(file.SerializeAsString())).status(), FileTransferSendStatus::kDisconnected);
    client->Exit();
    ASSERT_TRUE(module->Stop());
    EXPECT_EQ(module->ConnectedClientCount(), 0);
    EXPECT_FALSE(module->Address());
    ASSERT_TRUE(module->Destroy());
}

TEST_F(IrohFrontendTest, NetClientDialsAdmitsReceivesMediaAndStopsFromItsCallback) {
    RouteSessions();
    const auto address = server_->Address();
    ASSERT_TRUE(address);
    const auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{.runtime = runtime_});
    SdkConnectionParams parameters{};
    parameters.iroh_ = IrohDialParameters{.endpoint_address = *address,
                                          .endpoint_configuration = transport::testing::EndpointConfiguration(),
                                          .frontend = Parameters("net-client-media")};
    parameters.enable_video_ = true;
    parameters.stream_id_ = "net-client-media";
    const auto client = std::make_shared<NetClient>(std::move(parameters), notifier);
    const auto connected = std::make_shared<std::promise<void>>();
    auto ready = connected->get_future();
    const auto frame_received = std::make_shared<std::promise<std::string>>();
    auto received = frame_received->get_future();
    client->SetOnConnectCallback([connected] { connected->set_value(); });
    client->SetOnVideoFrameMsgCallback([owner = std::weak_ptr<NetClient>{client}, frame_received](EncodedVideoDelivery delivery) {
        if (const auto client = owner.lock()) client->Exit();
        frame_received->set_value(delivery.message->video_frame().data());
    });
    client->Start();
    ASSERT_EQ(ready.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto session = WaitForRoute();
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->SendVideo({.kind = media::VideoFrameKind::kIdr,
                                    .stream = 1,
                                    .width = 640,
                                    .height = 480,
                                    .frame_index = 1,
                                    .monitor = "application",
                                    .encoded = media::Packet(9000, 0x35)}));
    ASSERT_EQ(received.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(received.get(), std::string(9000, 0x35));
    client->Exit();
}

TEST_F(IrohFrontendTest, NetClientReadmitsAfterRelayOutageClosesQuic) {
    if (GetEnvironmentVariableA("PX_IROH_TEST_LONG_RELAY_OUTAGE", nullptr, 0) == 0) GTEST_SKIP() << "Requires the owned-Relay outage harness";
    RouteSessions();
    const auto address = server_->Address();
    ASSERT_TRUE(address);
    const auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{.runtime = runtime_});
    SdkConnectionParams parameters{};
    parameters.iroh_ = IrohDialParameters{.endpoint_address = *address,
                                          .endpoint_configuration = transport::testing::EndpointConfiguration(),
                                          .frontend = Parameters("reconnect-client")};
    auto client_configuration = nlohmann::json::parse(parameters.iroh_->endpoint_configuration);
    client_configuration["relay_only"] = true;
    parameters.iroh_->endpoint_configuration = client_configuration.dump();
    const auto refresh_count = std::make_shared<std::atomic_size_t>();
    parameters.iroh_->refresh_endpoint = [server = std::weak_ptr<IrohServer>{server_}, refresh_count,
                                          configuration = parameters.iroh_->endpoint_configuration]() -> std::optional<IrohConnectionDescription> {
        ++*refresh_count;
        const auto current = server.lock();
        if (!current) return std::nullopt;
        const auto refreshed = current->Address();
        if (refreshed) std::cout << "IROH_REFRESHED_ADDRESS " << *refreshed << std::endl;
        return refreshed ? std::optional<IrohConnectionDescription>{{*refreshed, configuration}} : std::nullopt;
    };
    parameters.enable_video_ = true;
    parameters.stream_id_ = "reconnect-client";
    const auto client = std::make_shared<NetClient>(std::move(parameters), notifier);
    const auto cleanup = PxScopeExit{[client] { client->Exit(); }};
    const auto connected = std::make_shared<std::atomic_size_t>();
    const auto reconnected = std::make_shared<std::promise<void>>();
    auto resumed = reconnected->get_future();
    const auto video_received = std::make_shared<std::promise<std::string>>();
    auto received = video_received->get_future();
    client->SetOnConnectCallback([connected, reconnected] {
        const auto count = connected->fetch_add(1) + 1;
        if (count == 2) reconnected->set_value();
    });
    client->SetOnDisconnectedCallback([] { std::cout << "IROH_QUIC_DISCONNECTED" << std::endl; });
    const auto initial_video = std::make_shared<std::atomic_bool>();
    const auto resumed_video = std::make_shared<std::atomic_bool>();
    client->SetOnVideoFrameMsgCallback([video_received, connected, initial_video, resumed_video, address](EncodedVideoDelivery delivery) {
        if (connected->load() == 1) {
            if (!initial_video->exchange(true)) std::cout << "IROH_RECONNECT_READY " << *address << std::endl;
        } else if (connected->load() == 2 && !resumed_video->exchange(true)) {
            video_received->set_value(delivery.message->video_frame().data());
        }
    });
    client->Start();
    // Keep real media flowing through the failed route: an idle connection does not
    // exercise the Relay queues that can obstruct the surviving candidate.
    std::jthread media_producer{[state = state_, connected](std::stop_token stop) {
        std::uint64_t frame_index{};
        while (!stop.stop_requested() && connected->load() < 2) {
            std::shared_ptr<IrohSession> current_session{};
            {
                std::lock_guard lock(state->mutex);
                current_session = state->routed;
            }
            if (current_session) {
                static_cast<void>(current_session->SendVideo({.kind = media::VideoFrameKind::kIdr,
                                                              .stream = 1,
                                                              .width = 640,
                                                              .height = 480,
                                                              .frame_index = ++frame_index,
                                                              .monitor = "application",
                                                              .encoded = media::Packet(9000, 0x35)}));
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
        }
    }};
    ASSERT_EQ(resumed.wait_for(std::chrono::seconds(75)), std::future_status::ready);
    std::shared_ptr<IrohSession> session{};
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->connected == 2; }));
        session = state_->routed;
    }
    ASSERT_TRUE(session);
    ASSERT_TRUE(session->SendVideo({.kind = media::VideoFrameKind::kIdr,
                                    .stream = 1,
                                    .width = 640,
                                    .height = 480,
                                    .frame_index = 1,
                                    .monitor = "application",
                                    .encoded = media::Packet(9000, 0x35)}));
    ASSERT_EQ(received.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(received.get(), std::string(9000, 0x35));
    client->Exit();
    EXPECT_EQ(connected->load(), 2);
    EXPECT_GT(refresh_count->load(), 0);
}

TEST_F(IrohFrontendTest, NetClientReportsAdmissionFailureWithoutConnectedCallback) {
    const auto address = server_->Address();
    ASSERT_TRUE(address);
    const auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{.runtime = runtime_});
    SdkConnectionParams parameters{};
    auto frontend = Parameters("rejected-net-client");
    frontend["safety_pwd_md5"] = "wrong-password";
    parameters.iroh_ = IrohDialParameters{
        .endpoint_address = *address, .endpoint_configuration = transport::testing::EndpointConfiguration(), .frontend = std::move(frontend)};
    const auto client = std::make_shared<NetClient>(std::move(parameters), notifier);
    const auto connected = std::make_shared<std::atomic_size_t>();
    const auto rejected = std::make_shared<std::promise<void>>();
    auto failed = rejected->get_future();
    client->SetOnConnectCallback([connected] { ++*connected; });
    client->SetOnDisconnectedCallback([rejected] { rejected->set_value(); });
    client->Start();
    ASSERT_EQ(failed.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    EXPECT_EQ(connected->load(), 0);
    client->Exit();
}

TEST_F(IrohFrontendTest, NetClientVoiceUsesBidirectionalDatagramsAndHonorsAudioRevocation) {
    RouteSessions();
    const auto address = server_->Address();
    ASSERT_TRUE(address);
    const auto notifier = std::make_shared<MessageNotifier>(MessageNotifierOptions{.runtime = runtime_});
    SdkConnectionParams parameters{};
    parameters.device_id_ = "voice-node";
    parameters.stream_id_ = "voice-stream";
    parameters.iroh_ = IrohDialParameters{
        .endpoint_address = *address, .endpoint_configuration = transport::testing::EndpointConfiguration(), .frontend = Parameters("voice-stream")};
    const auto client = std::make_shared<NetClient>(std::move(parameters), notifier);
    const auto connected = std::make_shared<std::promise<void>>();
    auto ready = connected->get_future();
    const auto returned_voice = std::make_shared<std::promise<std::string>>();
    auto returned = returned_voice->get_future();
    client->SetOnConnectCallback([connected] { connected->set_value(); });
    client->SetOnRawMessageCallback([returned_voice](std::shared_ptr<Message> message) {
        if (message->type() == kVoiceAudioFrame) returned_voice->set_value(message->voice_audio_frame().opus());
    });
    client->Start();
    ASSERT_EQ(ready.wait_for(std::chrono::seconds(5)), std::future_status::ready);
    const auto session = WaitForRoute();
    ASSERT_TRUE(session);
    const auto voice = std::make_shared<Message>();
    voice->set_type(kVoiceAudioFrame);
    voice->set_device_id("voice-node");
    voice->set_stream_id("voice-stream");
    auto& frame = *voice->mutable_voice_audio_frame();
    frame.set_call_id("accepted-voice-call");
    frame.set_sequence(42);
    frame.set_capture_time_ms(1234);
    frame.set_opus(std::string(160, 'v'));
    ASSERT_TRUE(client->PostVoiceAudioMessage(voice));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return !state->messages.empty(); }));
        const auto& event = state_->messages.front();
        EXPECT_EQ(event->channel_type_, TransportChannel::kMedia);
        Message received{};
        ASSERT_TRUE(received.ParseFromArray(event->message_->Bytes().data(), static_cast<int>(event->message_->Size())));
        EXPECT_EQ(received.stream_id(), "voice-stream");
        EXPECT_EQ(received.voice_audio_frame().SerializeAsString(), frame.SerializeAsString());
    }
    ASSERT_TRUE(session->SendVoice(*voice));
    ASSERT_EQ(returned.wait_for(std::chrono::seconds(3)), std::future_status::ready);
    EXPECT_EQ(returned.get(), frame.opus());
    frame.set_opus(std::string(1275, 'v'));
    EXPECT_FALSE(client->PostVoiceAudioMessage(voice));  // Over MTU: never silently move speech onto reliable control.
    frame.set_opus(std::string(160, 'v'));
    session->UpdatePermissions({"view"});
    EXPECT_FALSE(session->SendVoice(*voice));
    client->Exit();
    EXPECT_FALSE(client->PostVoiceAudioMessage(voice));
}

TEST_F(IrohFrontendTest, RenderMediaReachesSdkAndReferenceLossReturnsOverReliableControl) {
    RouteSessions();
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("media-stream"), 10000);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(opened->reply.accepted);
    const auto session = WaitForRoute();
    ASSERT_TRUE(session);
    const auto adapter = std::make_shared<IrohConnection>(nullptr, connection, opened->channels);
    adapter->SetMediaCallbacks(
        [state = state_](EncodedVideoDelivery delivery) {
            std::lock_guard lock(state->mutex);
            state->videos.push_back(std::move(delivery));
            state->changed.notify_all();
        },
        [state = state_](std::shared_ptr<Message> message) {
            std::lock_guard lock(state->mutex);
            state->audio.push_back(std::move(message));
            state->changed.notify_all();
        });
    adapter->Start();
    media::VideoFrame frame{.kind = media::VideoFrameKind::kIdr,
                            .stream = 1,
                            .width = 640,
                            .height = 480,
                            .frame_index = 40,
                            .monitor = "application",
                            .encoded = media::Packet(9000, 0x65)};
    ASSERT_TRUE(session->SendVideo(frame));
    for (std::size_t packet_index{}; packet_index < 8; ++packet_index) ASSERT_TRUE(session->SendAudio(media::Packet(100, 0x17)));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(
            state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->videos.size() == 1 && !state->audio.empty(); }));
        ASSERT_TRUE(state_->videos.front().dependency);
        EXPECT_EQ(state_->videos.front().dependency->kind, media::VideoFrameKind::kIdr);
        EXPECT_EQ(state_->videos.front().message->video_frame().data(), std::string(9000, 0x65));
        EXPECT_EQ(state_->audio.front()->audio_frame().data(), std::string(100, 0x17));
    }
    // Model a lost predicted frame: wire frame 2 never arrives; frame 3 depends on it.
    frame.kind = media::VideoFrameKind::kPredicted;
    frame.frame_index = 42;
    const auto packets = media::PacketizeVideoFrame(frame, {.frame_index = 3, .datagram_size = transport::kMediaDatagramBytes});
    ASSERT_TRUE(packets);
    for (const auto& packet : packets->packets) ASSERT_TRUE(session->SendDatagram(packet));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return !state->invalid_references.empty(); }));
        EXPECT_EQ(state_->invalid_references.front(), 41);
        EXPECT_EQ(state_->videos.size(), 1);
    }
    // The repaired frame uses kind 5 and may safely refer to the last delivered encoder frame.
    frame.kind = media::VideoFrameKind::kReferenceRecovery;
    frame.frame_index = 43;
    const auto repaired = media::PacketizeVideoFrame(frame, {.frame_index = 4, .datagram_size = transport::kMediaDatagramBytes});
    ASSERT_TRUE(repaired);
    for (const auto& packet : repaired->packets) ASSERT_TRUE(session->SendDatagram(packet));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->videos.size() == 2; }));
        ASSERT_TRUE(state_->videos.back().dependency);
        EXPECT_EQ(state_->videos.back().dependency->kind, media::VideoFrameKind::kReferenceRecovery);
        EXPECT_EQ(state_->videos.back().dependency->preceding_frame_index, 40);
    }
    adapter->Stop();
}

TEST_F(IrohFrontendTest, KeyFrameRequestBurstsAreCoalescedAndLaterRecoveryIsAllowed) {
    RouteSessions();
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("recovery-burst"), 10000);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(opened->reply.accepted);
    ASSERT_TRUE(WaitForRoute());
    Message recovery{};
    recovery.set_type(kMediaRecoveryRequest);
    recovery.mutable_media_recovery_request()->set_action(MediaRecoveryRequest::KEY_FRAME);
    recovery.mutable_media_recovery_request()->set_monitor("application");
    const auto encoded = recovery.SerializeAsString();
    const transport::Bytes wire(encoded.begin(), encoded.end());
    recovery.mutable_media_recovery_request()->set_action(MediaRecoveryRequest::INVALIDATE_REFERENCES);
    recovery.mutable_media_recovery_request()->set_invalid_reference_frame(42);
    const auto encoded_reference = recovery.SerializeAsString();
    const transport::Bytes reference_wire(encoded_reference.begin(), encoded_reference.end());
    Message decoder_request{};
    decoder_request.set_type(kInsertKeyFrame);
    const auto decoder_encoded = decoder_request.SerializeAsString();
    const transport::Bytes decoder_wire(decoder_encoded.begin(), decoder_encoded.end());
    for (std::size_t request_index{}; request_index < 20; ++request_index) {
        ASSERT_TRUE(opened->control->Send(wire, 3000));
        ASSERT_TRUE(opened->control->Send(reference_wire, 3000));
        ASSERT_TRUE(opened->control->Send(decoder_wire, 3000));
    }
    Message barrier{};
    barrier.set_type(kHeartBeat);
    const auto encoded_barrier = barrier.SerializeAsString();
    ASSERT_TRUE(opened->control->Send(transport::Bytes(encoded_barrier.begin(), encoded_barrier.end()), 3000));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return !state->messages.empty(); }));
        EXPECT_EQ(state_->key_frame_requests, 1);
        EXPECT_TRUE(state_->invalid_references.empty());
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    ASSERT_TRUE(opened->control->Send(reference_wire, 3000));
    ASSERT_TRUE(opened->control->Send(wire, 3000));
    ASSERT_TRUE(opened->control->Send(transport::Bytes(encoded_barrier.begin(), encoded_barrier.end()), 3000));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->messages.size() == 2; }));
        EXPECT_EQ(state_->key_frame_requests, 1);
        ASSERT_EQ(state_->invalid_references.size(), 1);
        EXPECT_EQ(state_->invalid_references.front(), 42);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(550));
    ASSERT_TRUE(opened->control->Send(wire, 3000));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->key_frame_requests == 2; }));
    }
    connection->Close();
}

TEST_F(IrohFrontendTest, AcceptedRepairAllowsNextIndependentLossWithoutCooldown) {
    RouteSessions();
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("consecutive-recovery"), 10000);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(opened->reply.accepted);
    const auto session = WaitForRoute();
    ASSERT_TRUE(session);
    Message recovery{};
    recovery.set_type(kMediaRecoveryRequest);
    recovery.mutable_media_recovery_request()->set_action(MediaRecoveryRequest::KEY_FRAME);
    recovery.mutable_media_recovery_request()->set_monitor("application");
    const auto encoded_request = recovery.SerializeAsString();
    const transport::Bytes request_wire(encoded_request.begin(), encoded_request.end());
    ASSERT_TRUE(opened->control->Send(request_wire, 3000));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->key_frame_requests == 1; }));
    }
    media::VideoFrame repaired{.kind = media::VideoFrameKind::kIdr,
                               .stream = 1,
                               .width = 640,
                               .height = 480,
                               .frame_index = 40,
                               .monitor = "application",
                               .encoded = media::Packet(100, 0x65)};
    for (const auto repair_kind : {media::VideoFrameKind::kIdr, media::VideoFrameKind::kReferenceRecovery}) {
        repaired.kind = repair_kind;
        ASSERT_TRUE(session->SendVideo(repaired));
        ++repaired.frame_index;
        ASSERT_TRUE(opened->control->Send(request_wire, 3000));
        Message barrier{};
        barrier.set_type(kHeartBeat);
        const auto encoded_barrier = barrier.SerializeAsString();
        ASSERT_TRUE(opened->control->Send(transport::Bytes(encoded_barrier.begin(), encoded_barrier.end()), 3000));
        std::unique_lock lock(state_->mutex);
        const auto expected_barriers = static_cast<std::size_t>(repaired.frame_index - 40);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_, expected_barriers] {
            return state->messages.size() == expected_barriers;
        }));
        EXPECT_EQ(state_->key_frame_requests, expected_barriers + 1);
    }
    connection->Close();
}

TEST_F(IrohFrontendTest, RealQuicAdmissionControlDatagramAndSingleDisconnect) {
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("stream-one"), 10000);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(opened->reply.accepted);
    ASSERT_TRUE(opened->control);
    std::shared_ptr<AcceptedIrohFrontend> accepted{};
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->connected == 1; }));
        accepted = state_->accepted;
    }
    ASSERT_TRUE(accepted);
    ASSERT_EQ(accepted->channels.size(), 4);
    ASSERT_EQ(opened->channels.size(), 4);
    EXPECT_TRUE(accepted->frontend->Allows("input"));
    EXPECT_NE(connection->PeerId(), accepted->connection->PeerId());
    const transport::Bytes control_payload{'c', 'o', 'n', 't', 'r', 'o', 'l'};
    ASSERT_TRUE(opened->control->Send(control_payload, 3000));
    EXPECT_EQ(accepted->control->Receive(3000), control_payload);
    const transport::Bytes input_payload{'i', 'n', 'p', 'u', 't'};
    ASSERT_TRUE(opened->channels.at(transport::ChannelKind::kInput)->Send(input_payload, 3000));
    EXPECT_EQ(accepted->channels.at(transport::ChannelKind::kInput)->Receive(3000), input_payload);
    const transport::Bytes media_payload{1, 2, 3, 4};
    ASSERT_TRUE(accepted->connection->SendDatagram(media_payload));
    EXPECT_EQ(connection->ReceiveDatagram(3000), media_payload);
    connection->Close();
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->disconnected == 1; }));
    }
    accepted->frontend->Close();
    server_->Stop();
    EXPECT_EQ(state_->disconnected, 1);
}

TEST_F(IrohFrontendTest, OccupiedControllerReturnsReasonWithoutAnotherOccupant) {
    const auto first = Connect();
    ASSERT_TRUE(first);
    const auto first_opened = transport::OpenFrontend(first, Parameters("stream-one"), 10000);
    ASSERT_TRUE(first_opened);
    ASSERT_TRUE(first_opened->reply.accepted);
    const auto second = Connect();
    ASSERT_TRUE(second);
    const auto denied = transport::OpenFrontend(second, Parameters("stream-two"), 10000);
    ASSERT_TRUE(denied);
    EXPECT_FALSE(denied->reply.accepted);
    EXPECT_EQ(denied->reply.code, "SESSION_OCCUPIED");
    EXPECT_FALSE(denied->control);
    EXPECT_FALSE(first->IsClosed());
    std::unique_lock lock(state_->mutex);
    ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->released_quotas > 0; }));
    EXPECT_EQ(state_->connected, 1);
}

TEST_F(IrohFrontendTest, WrongPasswordReturnsStructuredReason) {
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    auto parameters = Parameters("stream-wrong-password");
    parameters["safety_pwd_md5"] = "incorrect-password";
    const auto denied = transport::OpenFrontend(connection, parameters, 10000);
    ASSERT_TRUE(denied);
    EXPECT_FALSE(denied->reply.accepted);
    EXPECT_EQ(denied->reply.code, "SESSION_PASSWORD_REJECTED");
    EXPECT_EQ(state_->connected, 0);
}

TEST_F(IrohFrontendTest, StopInterruptsIncompleteHandshakeAndDoesNotCreateAnOccupant) {
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto control = transport::Channel::Open(connection, transport::ChannelKind::kControl, 3000);
    ASSERT_TRUE(control);
    const auto stop_started = std::chrono::steady_clock::now();
    server_->Stop();
    // iroh 1.3 Endpoint::close drains QUIC close notifications for about 3 seconds on an unconfirmed path.
    // Preserve that bounded graceful close; a 2-second local-only assumption rejects valid private Relay shutdown.
    EXPECT_LT(std::chrono::steady_clock::now() - stop_started, std::chrono::seconds(5));
    EXPECT_FALSE((*control)->Receive(1000));
    EXPECT_EQ(state_->connected, 0);
    EXPECT_EQ(state_->disconnected, 0);
    EXPECT_FALSE(server_->Start(transport::testing::EndpointConfiguration()));
}

TEST_F(IrohFrontendTest, BusinessMessagesRetainRouteAndUseIndependentInputStream) {
    RouteSessions();
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("stream-routed"), 10000);
    ASSERT_TRUE(opened);
    const auto route = WaitForRoute();
    ASSERT_TRUE(route);
    Message input{};
    input.set_type(kMouseEvent);
    input.set_stream_id("untrusted-other-stream");
    const auto wire = input.SerializeAsString();
    ASSERT_TRUE(opened->channels.at(transport::ChannelKind::kInput)->Send(transport::Bytes(wire.begin(), wire.end()), 3000));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->messages.size() == 1; }));
        const auto event = state_->messages.front();
        Message forwarded{};
        ASSERT_TRUE(forwarded.ParseFromString(event->message_->AsString()));
        EXPECT_EQ(forwarded.stream_id(), "stream-routed");
        EXPECT_EQ(event->connection_instance_id_, route->BindingId());
        EXPECT_EQ(event->transport_type_, TransportKind::kIroh);
        EXPECT_EQ(event->channel_type_, TransportChannel::kReliableControl);
    }
    Message reply{};
    reply.set_type(kOnHeartBeat);
    reply.mutable_on_heartbeat()->set_timestamp(123456);
    route->Send(Data::From(reply.SerializeAsString()));
    const auto received = opened->control->Receive(3000);
    ASSERT_TRUE(received);
    EXPECT_EQ(std::string(received->begin(), received->end()), reply.SerializeAsString());
    route->Close();
    route->Close();
    EXPECT_FALSE(route->IsAlive());
}

TEST_F(IrohFrontendTest, InputCannotBeSmuggledOverTheControlStream) {
    RouteSessions();
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    const auto opened = transport::OpenFrontend(connection, Parameters("stream-channel-mismatch"), 10000);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(WaitForRoute());
    Message input{};
    input.set_type(kKeyEvent);
    const auto wire = input.SerializeAsString();
    ASSERT_TRUE(opened->control->Send(transport::Bytes(wire.begin(), wire.end()), 3000));
    std::unique_lock lock(state_->mutex);
    ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->disconnected == 1; }));
    EXPECT_TRUE(state_->messages.empty());
}

TEST_F(IrohFrontendTest, RdpBytesCrossQuicAndLoopbackWithSingleFrontendAndImmediateClose) {
    const auto proxy = std::make_shared<asio2::tcp_server>();
    proxy->bind_recv([](std::shared_ptr<asio2::tcp_session>& socket, std::string_view payload) { socket->async_send(std::string(payload)); });
    ASSERT_TRUE(proxy->start("127.0.0.1", 0));
    RouteSessions(true, proxy->get_listen_port());
    const auto connection = Connect();
    ASSERT_TRUE(connection);
    auto parameters = Parameters("stream-rdp");
    parameters["rdp"] = "1";
    const auto opened = transport::OpenFrontend(connection, parameters, 10000);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(opened->reply.accepted);
    EXPECT_EQ(opened->channels.size(), 2);
    const auto route = WaitForRoute();
    ASSERT_TRUE(route);
    const auto rdp_channel = opened->channels.at(transport::ChannelKind::kRdp);
    const auto open_packet = rdp_channel->Receive(3000);
    ASSERT_TRUE(open_packet);
    const std::string open_wire(open_packet->begin(), open_packet->end());
    const auto binding = rdp::DecodeOpen(open_wire);
    ASSERT_TRUE(binding);
    const std::string binary_payload("RDP\0graphics\0input", 18);
    const auto outbound = rdp::EncodeData(*binding, binary_payload)->AsString();
    ASSERT_TRUE(rdp_channel->Send(transport::Bytes(outbound.begin(), outbound.end()), 3000));
    const auto echoed = rdp_channel->Receive(3000);
    ASSERT_TRUE(echoed);
    const std::string echoed_wire(echoed->begin(), echoed->end());
    const auto decoded = rdp::DecodePacket(*binding, echoed_wire);
    ASSERT_EQ(decoded.status, rdp::PacketStatus::kData);
    EXPECT_EQ(decoded.payload->AsString(), binary_payload);
    const auto duplicate = Connect();
    ASSERT_TRUE(duplicate);
    const auto denied = transport::OpenFrontend(duplicate, parameters, 10000);
    ASSERT_TRUE(denied);
    EXPECT_FALSE(denied->reply.accepted);
    EXPECT_EQ(denied->reply.code, "SESSION_OCCUPIED");
    EXPECT_TRUE(route->IsAlive());
    const auto close_wire = rdp::EncodeClose(*binding)->AsString();
    ASSERT_TRUE(rdp_channel->Send(transport::Bytes(close_wire.begin(), close_wire.end()), 3000));
    {
        std::unique_lock lock(state_->mutex);
        ASSERT_TRUE(state_->changed.wait_for(lock, std::chrono::seconds(3), [state = state_] { return state->disconnected == 1; }));
        EXPECT_TRUE(state_->messages.empty());
    }
    EXPECT_FALSE(route->IsAlive());
    proxy->stop();
}

}  // namespace
}  // namespace px
