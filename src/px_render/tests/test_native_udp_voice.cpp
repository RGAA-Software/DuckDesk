#include "network/udp/udp_transport.h"
#include "px_client_sdk/connection/udp_direct_connection.h"
#include "px_client_sdk/sdk_net_client.h"
#include "px_client_sdk/sdk_voice_protocol.h"
#include "px_common/message_notifier.h"
#include "px_common/time_util.h"

#include <chrono>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <vector>
#include <asio2/websocket/ws_server.hpp>
#include <gtest/gtest.h>

namespace px {
namespace {

using namespace std::chrono_literals;

struct VoiceWireProbe final {
    std::mutex mutex{};
    std::condition_variable condition{};
    unsigned bindings{};
    std::vector<std::shared_ptr<UdpVoiceFrameEvent>> uplink{};
    std::vector<UdpVoiceFrame> downlink{};
    std::atomic_uint reliable_files{};
    std::atomic_uint reliable_controls{};
    std::atomic_uint reliable_voice{};

    bool WaitFor(unsigned up, unsigned down, unsigned bound = 1) {
        std::unique_lock lock(mutex);
        return condition.wait_for(lock, 2s, [&] { return uplink.size() >= up && downlink.size() >= down && bindings >= bound; });
    }
};

struct NativeVoiceWire final {
    const std::shared_ptr<PxAsyncRuntime> runtime{PxAsyncRuntime::Create({.worker_threads = 1})};
    const std::shared_ptr<MessageNotifier> notifier{std::make_shared<MessageNotifier>()};
    const std::shared_ptr<VoiceWireProbe> probe{std::make_shared<VoiceWireProbe>()};
    const std::shared_ptr<UdpTransport> render{std::make_shared<UdpTransport>(runtime)};
    std::shared_ptr<UdpDirectConnection> client{};
    int port{};

    ~NativeVoiceWire() {
        if (client) {
            client->Stop();
        }
        static_cast<void>(render->Destroy());
        notifier->Stop(MessageBusStopMode::kCancel);
        if (runtime) {
            runtime->RequestDrain();
            runtime->Join();
        }
    }

    bool Start(bool start_client = true) {
        if (!runtime || !runtime->Start()) {
            return false;
        }
        const auto weak_probe = std::weak_ptr(probe);
        render->SetEventCallback([weak_probe](const RenderEventEnvelope& envelope) {
            if (const auto active = weak_probe.lock()) {
                std::visit(
                    [&active](const auto& event) {
                        using Event = typename std::decay_t<decltype(event)>::element_type;
                        std::lock_guard lock(active->mutex);
                        if constexpr (std::is_same_v<Event, UdpVoiceFrameEvent>) {
                            active->uplink.push_back(event);
                        } else if constexpr (std::is_same_v<Event, StreamingParametersRequestedEvent>) {
                            ++active->bindings;
                        }
                    },
                    envelope.payload);
                active->condition.notify_all();
            }
        });
        RenderModuleConfiguration configuration{};
        configuration.async_runtime = runtime;
        port = 45000 + static_cast<int>(GetCurrentProcessId() % 10000);
        configuration.udp_listen_port = port;
        if (!render->Start(configuration)) {
            return false;
        }
        UpdateAssociation("association-a", false);
        if (start_client) {
            StartClient("association-a");
            return probe->WaitFor(0, 0);
        }
        return true;
    }

    void StartClient(const std::string& association) {
        client = std::make_shared<UdpDirectConnection>(notifier);
        const auto weak_probe = std::weak_ptr(probe);
        client->SetOnVoiceFrameCallback([weak_probe](UdpVoiceFrame frame) {
            if (const auto active = weak_probe.lock()) {
                {
                    std::lock_guard lock(active->mutex);
                    active->downlink.push_back(std::move(frame));
                }
                active->condition.notify_all();
            }
        });
        client->Start("127.0.0.1", port, "stream", association);
    }

    void UpdateAssociation(const std::string& association, bool revoke) {
        render->UpdateUdpMediaAssociation({
            .association_code_ = association,
            .logical_session_id_ = "logical-session",
            .stream_id_ = "stream",
            .expires_at_ms_ = static_cast<std::int64_t>(TimeUtil::GetCurrentTimestamp()) + 30000,
            .revoke_ = revoke,
        });
    }
};

struct ReliableVoicePeer final {
    const std::shared_ptr<asio2::ws_server> server{std::make_shared<asio2::ws_server>()};
    std::shared_ptr<NetClient> client{};
    ~ReliableVoicePeer() {
        if (client) {
            client->Exit();
        }
        server->stop();
    }
};

TEST(NativeUdpVoice, SdkRoutesVoiceToUdpWhileFilesAndCallControlRemainOnWebSocket) {
    NativeVoiceWire session{};
    ASSERT_TRUE(session.Start(false));
    ReliableVoicePeer reliable{};
    const auto weak_probe = std::weak_ptr(session.probe);
    reliable.server->bind_accept([](const std::shared_ptr<asio2::ws_session>& peer) { peer->ws_stream().binary(true); });
    reliable.server->bind_upgrade([](const std::shared_ptr<asio2::ws_session>& peer) {
        Message configuration{};
        configuration.set_type(kServerConfiguration);
        peer->async_send(configuration.SerializeAsString());
    });
    reliable.server->bind_recv([weak_probe](const std::shared_ptr<asio2::ws_session>&, std::string_view payload) {
        const auto probe = weak_probe.lock();
        if (!probe) {
            return;
        }
        if (payload.size() == 32768U && payload.front() == 'f') {
            ++probe->reliable_files;
        } else {
            Message message{};
            // Protobuf synchronous test-peer boundary; no borrowed bytes survive this callback.
            if (message.ParseFromArray(payload.data(), static_cast<int>(payload.size()))) {
                if (message.type() == kVoiceAudioFrame) {
                    ++probe->reliable_voice;
                } else if (message.type() == kVoiceCallRequest) {
                    ++probe->reliable_controls;
                }
            }
        }
        probe->condition.notify_all();
    });
    ASSERT_TRUE(reliable.server->start("127.0.0.1", 0));
    SdkConnectionParams params{
        .enable_video_ = true,
        .ip_ = "127.0.0.1",
        .port_ = reliable.server->listen_port(),
        .udp_port_ = session.port,
        .media_path_ = "/media?udp_media=1",
        .device_id_ = "client",
        .stream_id_ = "stream",
        .udp_media_association_ = "association-a",
    };
    reliable.client = std::make_shared<NetClient>(std::move(params), session.notifier);
    reliable.client->SetOnRawMessageCallback([weak_probe](const std::shared_ptr<Message>& message) {
        if (message->type() != kVoiceAudioFrame || !message->has_voice_audio_frame()) {
            return;
        }
        if (const auto probe = weak_probe.lock()) {
            const auto& audio = message->voice_audio_frame();
            {
                std::lock_guard lock(probe->mutex);
                probe->downlink.push_back({.call_id = audio.call_id(),
                                           .sequence = audio.sequence(),
                                           .capture_time_ms = audio.capture_time_ms(),
                                           .opus = {audio.opus().begin(), audio.opus().end()}});
            }
            probe->condition.notify_all();
        }
    });
    reliable.client->Start();
    ASSERT_TRUE(session.probe->WaitFor(0, 0));
    const auto file = Data::From(std::string(32768U, 'f'));
    const auto sdk = reliable.client;
    std::jthread files([sdk, file] {
        for (unsigned index{}; index < 32; ++index) {
            static_cast<void>(sdk->PostFileTransferMessage(file));
        }
    });
    unsigned sent{};
    for (std::uint32_t sequence{}; sequence < 8; ++sequence) {
        const auto message =
            std::make_shared<Message>(MakeVoiceAudioFrameMessage("client", "stream", "call", sequence, 1, std::vector<std::uint8_t>{1, 2, 3}));
        sent += sdk->PostVoiceAudioMessage(message) ? 1U : 0U;
    }
    ASSERT_GT(sent, 0U);
    const auto request = MakeVoiceCallRequestMessage("client", "stream", "call", 1, true);
    sdk->PostMediaMessage(Data::From(request.SerializeAsString()));
    ASSERT_TRUE(session.render->SendVoiceFrame("stream", {.call_id = "call", .sequence = 10, .opus = {1, 2, 3}}));
    files.join();
    ASSERT_TRUE(session.probe->WaitFor(sent, 1));
    {
        const auto probe = session.probe;
        std::unique_lock lock(probe->mutex);
        EXPECT_TRUE(probe->condition.wait_for(lock, 2s, [probe] { return probe->reliable_files > 0 && probe->reliable_controls == 1; }));
    }
    sdk->Exit();
    EXPECT_EQ(session.probe->reliable_voice, 0U);
    EXPECT_GT(session.probe->reliable_files, 0U);
    EXPECT_EQ(session.probe->reliable_controls, 1U);
}

TEST(NativeUdpVoice, RealDuplexFramesUseBoundStreamAndIndependentPacketType) {
    NativeVoiceWire session{};
    ASSERT_TRUE(session.Start());
    const UdpVoiceFrame frame{.call_id = "call", .sequence = 19, .capture_time_ms = 0x100000001ULL, .opus = {1, 2, 3}};
    ASSERT_TRUE(session.client->PostVoiceFrame(frame.call_id, frame.sequence, frame.capture_time_ms, frame.opus));
    ASSERT_TRUE(session.render->SendVoiceFrame("stream", frame));
    ASSERT_TRUE(session.probe->WaitFor(1, 1));
    std::lock_guard lock(session.probe->mutex);
    const auto& up = session.probe->uplink.front();
    EXPECT_EQ(up->logical_session_id, "logical-session");
    EXPECT_EQ(up->stream_id, "stream");
    EXPECT_TRUE(up->is_current_binding());
    EXPECT_EQ(up->frame->call_id, frame.call_id);
    EXPECT_EQ(up->frame->capture_time_ms, frame.capture_time_ms);
    EXPECT_EQ(up->frame->opus, frame.opus);
    const auto& down = session.probe->downlink.front();
    EXPECT_EQ(down.association_code, "association-a");
    EXPECT_EQ(down.sequence, frame.sequence);
    EXPECT_EQ(down.opus, frame.opus);
    EXPECT_FALSE(session.render->SendVoiceFrame("another-stream", frame));
}

TEST(NativeUdpVoice, RevocationInvalidatesQueuedEventsAndOldAssociationCannotReenter) {
    NativeVoiceWire session{};
    ASSERT_TRUE(session.Start());
    const std::vector<std::uint8_t> opus{1, 2, 3};
    ASSERT_TRUE(session.client->PostVoiceFrame("call-a", 1, 1, opus));
    ASSERT_TRUE(session.probe->WaitFor(1, 0));
    std::shared_ptr<UdpVoiceFrameEvent> old{};
    {
        std::lock_guard lock(session.probe->mutex);
        old = session.probe->uplink.front();
    }
    session.UpdateAssociation("association-a", true);
    EXPECT_FALSE(old->is_current_binding());
    EXPECT_FALSE(session.render->SendVoiceFrame("stream", {.call_id = "call-a", .opus = opus}));
    session.client->Stop();
    session.UpdateAssociation("association-b", false);
    session.StartClient("association-b");
    ASSERT_TRUE(session.probe->WaitFor(1, 0, 2));
    // Inject an old binding from the current endpoint, followed by a valid marker from that same socket.
    session.client->PostBinaryMessage(UdpVoiceProtocol::Build("association-a", "call-b", 2, 2, opus));
    ASSERT_TRUE(session.client->PostVoiceFrame("call-b", 3, 3, opus));
    ASSERT_TRUE(session.probe->WaitFor(2, 0, 2));
    session.client->Stop();
    static_cast<void>(session.render->Destroy());
    EXPECT_FALSE(old->is_current_binding());
    std::lock_guard lock(session.probe->mutex);
    ASSERT_EQ(session.probe->uplink.size(), 2U);
    EXPECT_EQ(session.probe->uplink.back()->frame->sequence, 3U);
    EXPECT_FALSE(session.probe->uplink.back()->is_current_binding());
}

TEST(NativeUdpVoice, UnknownAssociationNeverBindsOrProducesVoiceEvents) {
    NativeVoiceWire session{};
    ASSERT_TRUE(session.Start());
    const auto stranger = std::make_shared<UdpDirectConnection>(session.notifier);
    const auto connected = std::make_shared<std::promise<void>>();
    auto ready = connected->get_future();
    stranger->RegisterOnConnectedCallback([connected] { connected->set_value(); });
    stranger->Start("127.0.0.1", session.port, "stream", "unknown-association");
    ASSERT_EQ(ready.wait_for(2s), std::future_status::ready);
    EXPECT_TRUE(stranger->PostVoiceFrame("call", 1, 1, std::vector<std::uint8_t>{1}));
    stranger->Stop();
    EXPECT_FALSE(session.probe->WaitFor(1, 0));
}

TEST(NativeUdpVoice, ClientCanStopInsideVoiceDeliveryAndDoesNotResurrect) {
    NativeVoiceWire session{};
    ASSERT_TRUE(session.Start());
    session.client->Stop();
    session.UpdateAssociation("association-a", true);
    session.UpdateAssociation("association-b", false);
    session.client = std::make_shared<UdpDirectConnection>(session.notifier);
    const auto weak_client = std::weak_ptr(session.client);
    const auto stopped = std::make_shared<std::promise<void>>();
    auto done = stopped->get_future();
    session.client->SetOnVoiceFrameCallback([weak_client, stopped](UdpVoiceFrame) {
        if (const auto client = weak_client.lock()) {
            client->Stop();
        }
        stopped->set_value();
    });
    session.client->Start("127.0.0.1", session.port, "stream", "association-b");
    ASSERT_TRUE(session.probe->WaitFor(0, 0, 2));
    ASSERT_TRUE(session.render->SendVoiceFrame("stream", {.call_id = "call", .opus = {1}}));
    ASSERT_EQ(done.wait_for(2s), std::future_status::ready);
    EXPECT_FALSE(session.client->PostVoiceFrame("call", 1, 1, std::vector<std::uint8_t>{1}));
    session.client->Stop();
}

} // namespace
} // namespace px
