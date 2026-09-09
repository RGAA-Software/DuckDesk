#include <gtest/gtest.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

#include "px_message.pb.h"
#include "px_rdp/rdp_tcp_bridge.h"
#include "px_rdp/rdp_workspace_lease.h"
#include "px_rdp/rdp_proxy_policy.h"
#include "px_rdp/rdp_private_directory.h"
#include "px_rdp/rdp_client_endpoint.h"
#include "px_rdp/rdp_frontend_lease.h"
#include "px_rdp/rdp_control_lease.h"
#include "px_common/reliable_websocket_send.h"
#include "px_common/win32/render_instance_lease.h"

namespace {

using namespace std::chrono_literals;
using namespace px::rdp;

TEST(RdpControlLease, ServiceLossExpiresButBriefInterruptionsRecover) {
    const auto start = RdpControlLease::Clock::time_point{};
    RdpControlLease lease{};
    EXPECT_TRUE(lease.Observe(true, start));
    EXPECT_TRUE(lease.Observe(false, start + 1s));
    EXPECT_TRUE(lease.Observe(false, start + 5s));
    EXPECT_TRUE(lease.Observe(true, start + 5s));
    EXPECT_TRUE(lease.Observe(false, start + 6s));
    EXPECT_FALSE(lease.Observe(false, start + 11s));
    EXPECT_FALSE(lease.Observe(true, start + 12s));
    RdpControlLease fresh{};
    EXPECT_TRUE(fresh.Observe(true, start + 12s));
}

TEST(RenderInstanceLease, ValidKernelNameAndRepeatedRelease) {
    DWORD error{};
    EXPECT_FALSE(px::AcquireRenderInstanceLease(0, error));
    EXPECT_EQ(error, ERROR_INVALID_PARAMETER);
    for (int iteration{}; iteration < 5; ++iteration) {
        auto first = px::AcquireRenderInstanceLease(65431, error);
        ASSERT_TRUE(first);
        EXPECT_EQ(error, ERROR_SUCCESS);
        EXPECT_FALSE(px::AcquireRenderInstanceLease(65431, error));
        EXPECT_EQ(error, ERROR_ALREADY_EXISTS);
        first.reset();
        EXPECT_TRUE(px::AcquireRenderInstanceLease(65431, error));
    }
}

TEST(RdpFrontendLease, SingleClientRecoveryAndLateRelease) {
    FrontendLease lease{};
    EXPECT_FALSE(lease.Acquire(""));
    const auto first = lease.Acquire("guest-logical-session-1");
    ASSERT_TRUE(first);
    EXPECT_FALSE(lease.Acquire("guest-logical-session-1"));
    EXPECT_FALSE(lease.Acquire("guest-logical-session-2"));
    lease.Release(*first);
    EXPECT_FALSE(lease.Acquire("guest-logical-session-2"));
    const auto recovered = lease.Acquire("guest-logical-session-1");
    ASSERT_TRUE(recovered);
    lease.Release(*first);
    EXPECT_FALSE(lease.Acquire("guest-logical-session-1"));
    lease.Release(*recovered);
    EXPECT_TRUE(lease.Acquire("guest-logical-session-1"));
}

StreamBinding Binding() {
    return {.connection_id = "authorized-test-connection", .generation = 7};
}

#ifdef _WIN32
class LeaseDirectory final {
  public:
    LeaseDirectory()
        : path_(std::filesystem::temp_directory_path() / ("gammaray-rdp-lease-" + std::to_string(GetCurrentProcessId()) + "-" +
                                                          std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        if (!OpenPrivateRdpDirectory(path_, true)) {
            throw std::runtime_error("lease test directory creation failed");
        }
    }
    ~LeaseDirectory() {
        std::error_code error{};
        std::filesystem::remove_all(path_, error);
    }
    [[nodiscard]] const std::filesystem::path& Path() const noexcept {
        return path_;
    }

  private:
    std::filesystem::path path_{};
};

TEST(RdpWorkspaceLease, SingleOwnerAndReleaseWithoutDeletingPersistentMarker) {
    const LeaseDirectory directory{};
    for (int iteration{0}; iteration < 10; ++iteration) {
        auto lease = RdpWorkspaceLease::Acquire(directory.Path(), "workspace-1");
        ASSERT_NE(lease, nullptr);
        EXPECT_EQ(RdpWorkspaceLease::Acquire(directory.Path(), "workspace-1"), nullptr);
        const auto different = RdpWorkspaceLease::Acquire(directory.Path(), "workspace-2");
        EXPECT_NE(different, nullptr);
        lease.reset();
        EXPECT_TRUE(std::filesystem::exists(directory.Path() / "workspace-1.runtime.lock"));
        EXPECT_NE(RdpWorkspaceLease::Acquire(directory.Path(), "workspace-1"), nullptr);
    }
}

TEST(RdpWorkspaceLease, InvalidNamesAndRelativeRootFailClosed) {
    const LeaseDirectory directory{};
    for (const auto& name : std::vector<std::string>{"", "../escape", "workspace:stream", "a/b", "a\\b", std::string(129, 'a')}) {
        EXPECT_EQ(RdpWorkspaceLease::Acquire(directory.Path(), name), nullptr);
    }
    EXPECT_EQ(RdpWorkspaceLease::Acquire("relative-path", "workspace-1"), nullptr);
}
#endif

TEST(RdpStreamPacket, OpenAssignsValidBindingButCannotBeInjectedIntoActiveByteStream) {
    const auto binding = Binding();
    const auto wire = EncodeOpen(binding);
    ASSERT_NE(wire, nullptr);
    px::Message envelope{};
    ASSERT_TRUE(envelope.ParseFromArray(wire->Bytes().data(), static_cast<int>(wire->Size())));
    EXPECT_EQ(envelope.type(), px::kRdpStream);
    EXPECT_EQ(envelope.rdp_stream().kind(), px::RdpStreamPacket::OPEN);
    EXPECT_EQ(envelope.rdp_stream().connection_id(), binding.connection_id);
    EXPECT_EQ(envelope.rdp_stream().generation(), binding.generation);
    EXPECT_TRUE(envelope.rdp_stream().payload().empty());
    EXPECT_EQ(DecodePacket(binding, wire->Bytes()).status, PacketStatus::kInvalid);
    EXPECT_EQ(EncodeOpen(StreamBinding{}), nullptr);
    const auto decoded = DecodeOpen(wire->Bytes());
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->connection_id, binding.connection_id);
    EXPECT_EQ(decoded->generation, binding.generation);
    EXPECT_FALSE(DecodeOpen(EncodeClose(binding)->Bytes()));
    EXPECT_FALSE(DecodeOpen(EncodeData(binding, std::span<const char>{"abc", 3})->Bytes()));
    envelope.mutable_rdp_stream()->set_generation(0); // Transient protobuf-owned submessage ABI.
    EXPECT_FALSE(DecodeOpen(px::Data::From(envelope.SerializeAsString())->Bytes()));
    envelope.mutable_rdp_stream()->set_generation(1);
    envelope.mutable_rdp_stream()->set_payload("unexpected");
    EXPECT_FALSE(DecodeOpen(px::Data::From(envelope.SerializeAsString())->Bytes()));
}

TEST(RdpProxyPolicy, OnlyExactManagedLocalIdentityIsAdmitted) {
    EXPECT_TRUE(IsWorkspacePeer("grdp_account", "NODE", "GRDP_ACCOUNT", "node"));
    EXPECT_FALSE(IsWorkspacePeer("grdp_account", "NODE", "Administrator", "NODE"));
    EXPECT_FALSE(IsWorkspacePeer("grdp_account", "NODE", "grdp_other", "NODE"));
    EXPECT_FALSE(IsWorkspacePeer("grdp_account", "NODE", "grdp_account", "DOMAIN"));
    EXPECT_FALSE(IsWorkspacePeer("grdp_account", "NODE", "grdp_account", ""));
    EXPECT_FALSE(IsWorkspacePeer("Administrator", "NODE", "Administrator", "NODE"));
}

TEST(RdpProxyPolicy, UnknownDeviceAndAuxiliaryChannelsStayDisabled) {
    EXPECT_TRUE(IsAllowedStaticChannel("cliprdr"));
    EXPECT_TRUE(IsAllowedStaticChannel("rdpsnd"));
    EXPECT_TRUE(IsAllowedStaticChannel("drdynvc"));
    EXPECT_TRUE(IsAllowedStaticChannel("rdpdr")); // Restricted to handshake and zero devices by its data filter.
    EXPECT_FALSE(IsAllowedStaticChannel("rail"));
    EXPECT_FALSE(IsAllowedStaticChannel("unknown"));
    EXPECT_TRUE(IsAllowedDynamicChannel("Microsoft::Windows::RDS::Graphics"));
    EXPECT_TRUE(IsAllowedDynamicChannel("Microsoft::Windows::RDS::DisplayControl"));
    EXPECT_TRUE(IsAllowedDynamicChannel("AUDIO_PLAYBACK_DVC"));
    EXPECT_TRUE(IsAllowedDynamicChannel("AUDIO_PLAYBACK_LOSSY_DVC"));
    EXPECT_FALSE(IsAllowedDynamicChannel("AUDIO_INPUT"));
    EXPECT_FALSE(IsAllowedDynamicChannel("AUDIO_INPUT_DVC"));
    EXPECT_FALSE(IsAllowedDynamicChannel("Microsoft::Windows::RDS::Video::Data::v08.01"));
    EXPECT_FALSE(IsAllowedDynamicChannel("unknown"));
}

TEST(RdpProxyPolicy, AudioDeviceHandshakeNeverAllowsDevicesOrIo) {
    constexpr auto client = DeviceChannelDirection::kClientToServer;
    constexpr auto server = DeviceChannelDirection::kServerToClient;
    auto empty = std::vector<unsigned char>{0x72, 0x44, 0x41, 0x44, 0, 0, 0, 0};
    EXPECT_TRUE(IsAudioDeviceHandshake(client, empty, 3, empty.size()));
    EXPECT_FALSE(IsAudioDeviceHandshake(server, empty, 3, empty.size()));
    empty[4] = 1;
    EXPECT_FALSE(IsAudioDeviceHandshake(client, empty, 3, empty.size()));
    empty[4] = 0;
    EXPECT_FALSE(IsAudioDeviceHandshake(client, empty, 1, empty.size()));
    EXPECT_FALSE(IsAudioDeviceHandshake(client, empty, 3, empty.size() + 1));
    EXPECT_FALSE(IsAudioDeviceHandshake(client, empty, 0x23, empty.size()));
    for (const auto packet : {0x4952u, 0x4943u, 0x6472u, 0x5043u, 0xffffu}) {
        empty[2] = static_cast<unsigned char>(packet);
        empty[3] = static_cast<unsigned char>(packet >> 8);
        EXPECT_FALSE(IsAudioDeviceHandshake(client, empty, 3, empty.size()));
        EXPECT_FALSE(IsAudioDeviceHandshake(server, empty, 3, empty.size()));
    }
    const auto announce = std::array<unsigned char, 12>{0x72, 0x44, 0x6e, 0x49, 1, 0, 0x0d, 0, 1, 0, 0, 0};
    EXPECT_TRUE(IsAudioDeviceHandshake(server, announce, 3, announce.size()));
    EXPECT_FALSE(IsAudioDeviceHandshake(client, announce, 3, announce.size()));
    for (std::size_t size{}; size < announce.size(); ++size) {
        EXPECT_FALSE(IsAudioDeviceHandshake(server, std::span{announce}.first(size), 3, size));
    }
    auto capabilities = std::vector<unsigned char>{0x72, 0x44, 0x50, 0x43, 1, 0, 0, 0, 1, 0, 8, 0, 1, 0, 0, 0};
    EXPECT_TRUE(IsAudioDeviceHandshake(client, capabilities, 3, capabilities.size()));
    EXPECT_FALSE(IsAudioDeviceHandshake(server, capabilities, 3, capabilities.size()));
    capabilities[10] = 9;
    EXPECT_FALSE(IsAudioDeviceHandshake(client, capabilities, 3, capabilities.size()));
    capabilities[10] = 8;
    capabilities[8] = 6;
    EXPECT_FALSE(IsAudioDeviceHandshake(client, capabilities, 3, capabilities.size()));
}

#ifdef _WIN32
TEST(RdpProxyPolicy, CertificateWithoutValidDeploymentPinFailsClosed) {
    const auto bytes = std::array<unsigned char, 3>{1, 2, 3};
    EXPECT_FALSE(VerifyPinnedCertificate({}, std::string(64, '0')));
    EXPECT_FALSE(VerifyPinnedCertificate(bytes, ""));
    EXPECT_FALSE(VerifyPinnedCertificate(bytes, std::string(64, 'g')));
    EXPECT_FALSE(VerifyPinnedCertificate(bytes, std::string(64, '0')));
}
#endif

void Pump(const std::shared_ptr<asio::io_context>& io) {
    io->restart();
    io->run_for(10ms);
}

template <typename Predicate> bool PumpUntil(const std::shared_ptr<asio::io_context>& io, Predicate ready) {
    const auto deadline = std::chrono::steady_clock::now() + 2s;
    do {
        Pump(io);
        if (ready()) {
            return true;
        }
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

struct SocketPair final {
    std::shared_ptr<asio::ip::tcp::socket> local{};
    std::shared_ptr<asio::ip::tcp::socket> peer{};
};

SocketPair ConnectPair(const std::shared_ptr<asio::io_context>& io) {
    asio::ip::tcp::acceptor acceptor(*io, {asio::ip::address_v4::loopback(), 0});
    auto local = std::make_shared<asio::ip::tcp::socket>(*io);
    auto peer = std::make_shared<asio::ip::tcp::socket>(*io);
    peer->connect(acceptor.local_endpoint());
    acceptor.accept(*local);
    return {.local = std::move(local), .peer = std::move(peer)};
}

std::string ReadAvailable(const std::shared_ptr<asio::ip::tcp::socket>& socket) {
    const auto count = socket->available();
    if (count == 0) {
        return {};
    }
    const auto buffer = px::Data::Allocate(count);
    const auto received = socket->read_some(asio::buffer(buffer->MutableBytes().data(), count));
    return std::string(buffer->Bytes().first(received).begin(), buffer->Bytes().first(received).end());
}

#ifdef _WIN32
struct EndpointObserver final {
    std::uint16_t port{0};
    std::vector<BridgeCloseReason> closed{};
    std::string bytes{};
    std::weak_ptr<RdpClientEndpoint> endpoint{};
    bool stop_on_ready{false};
};

std::shared_ptr<RdpClientEndpoint> MakeEndpoint(const std::shared_ptr<asio::io_context>& io, const std::shared_ptr<EndpointObserver>& observer,
                                                BridgeOptions options = {}) {
    const auto weak = std::weak_ptr<EndpointObserver>{observer};
    auto endpoint = RdpClientEndpoint::Create(
        io->get_executor(), Binding(),
        [weak](std::shared_ptr<px::Data> wire, RdpTcpBridge::SendCompletion done) {
            if (const auto current = weak.lock()) {
                const auto packet = DecodePacket(Binding(), wire->Bytes());
                if (packet.payload) {
                    current->bytes.append(packet.payload->Bytes().begin(), packet.payload->Bytes().end());
                }
                done(true);
            } else {
                done(false);
            }
        },
        [weak](std::uint16_t port) {
            if (const auto current = weak.lock()) {
                current->port = port;
                if (current->stop_on_ready) {
                    if (const auto owner = current->endpoint.lock()) {
                        owner->Stop();
                    }
                }
            }
        },
        [weak](BridgeCloseReason reason) {
            if (const auto current = weak.lock()) {
                current->closed.push_back(reason);
                if (const auto owner = current->endpoint.lock()) {
                    owner->Stop(); // Shutdown from notification must remain idempotent.
                }
            }
        },
        options);
    observer->endpoint = endpoint;
    return endpoint;
}

TEST(RdpClientEndpoint, SameProcessByteStreamAndRepeatedStop) {
    const auto io = std::make_shared<asio::io_context>();
    for (unsigned int iteration{}; iteration < 5; ++iteration) {
        const auto observer = std::make_shared<EndpointObserver>();
        const auto endpoint = MakeEndpoint(io, observer);
        ASSERT_NE(endpoint, nullptr);
        ASSERT_TRUE(PumpUntil(io, [observer] { return observer->port != 0; }));
        const auto socket = std::make_shared<asio::ip::tcp::socket>(*io);
        socket->connect({asio::ip::address_v4::loopback(), observer->port});
        asio::write(*socket, asio::buffer("from-freerdp", 12));
        ASSERT_TRUE(PumpUntil(io, [observer] { return observer->bytes == "from-freerdp"; }));
        EXPECT_TRUE(endpoint->Receive(EncodeData(Binding(), std::span<const char>{"from-render", 11})));
        ASSERT_TRUE(PumpUntil(io, [socket] { return socket->available() == 11; }));
        EXPECT_EQ(ReadAvailable(socket), "from-render");
        endpoint->Stop();
        endpoint->Stop();
        ASSERT_TRUE(PumpUntil(io, [observer] { return !observer->closed.empty(); }));
        Pump(io);
        EXPECT_EQ(observer->closed.size(), 1);
        EXPECT_FALSE(endpoint->Receive(EncodeClose(Binding())));
    }
}

TEST(RdpClientEndpoint, TimeoutAndStopFromReadyReleaseListener) {
    const auto io = std::make_shared<asio::io_context>();
    for (const bool stop_on_ready : {false, true}) {
        const auto observer = std::make_shared<EndpointObserver>();
        observer->stop_on_ready = stop_on_ready;
        auto options = BridgeOptions{};
        options.connect_timeout = 20ms;
        const auto endpoint = MakeEndpoint(io, observer, options);
        ASSERT_TRUE(PumpUntil(io, [observer] { return !observer->closed.empty(); }));
        EXPECT_EQ(observer->closed.front(), stop_on_ready ? BridgeCloseReason::kStopped : BridgeCloseReason::kTimedOut);
        asio::error_code error{};
        asio::ip::tcp::socket socket{*io};
        socket.connect({asio::ip::address_v4::loopback(), observer->port}, error);
        EXPECT_TRUE(error);
    }
}

TEST(RdpClientEndpoint, DestructionWithQueuedStartAndAcceptHasNoLateCallback) {
    const auto io = std::make_shared<asio::io_context>();
    for (const bool start : {false, true}) {
        const auto observer = std::make_shared<EndpointObserver>();
        auto endpoint = MakeEndpoint(io, observer);
        if (start) {
            ASSERT_TRUE(PumpUntil(io, [observer] { return observer->port != 0; }));
        }
        endpoint.reset();
        Pump(io);
        EXPECT_TRUE(observer->endpoint.expired());
        EXPECT_TRUE(observer->closed.empty());
        if (!start) {
            EXPECT_EQ(observer->port, 0);
        }
    }
}
#endif

struct Observer final {
    std::vector<DecodedPacket> packets{};
    std::vector<BridgeCloseReason> closed{};
    RdpTcpBridge::SendCompletion delayed{};
    std::weak_ptr<RdpTcpBridge> bridge{};
    bool delay_send{false};
    bool stop_in_callback{false};
};

std::shared_ptr<RdpTcpBridge> MakeBridge(const std::shared_ptr<asio::io_context>& io, const std::shared_ptr<Observer>& observer,
                                         BridgeOptions options = {}) {
    const auto weak = std::weak_ptr<Observer>(observer);
    return RdpTcpBridge::Create(
        io->get_executor(), Binding(),
        [weak](std::shared_ptr<px::Data> wire, RdpTcpBridge::SendCompletion complete) {
            if (const auto current = weak.lock()) {
                current->packets.push_back(DecodePacket(Binding(), wire->Bytes()));
                if (current->stop_in_callback) {
                    if (const auto owner = current->bridge.lock()) {
                        owner->Stop();
                    }
                }
                if (current->delay_send && current->packets.back().status == PacketStatus::kData) {
                    current->delayed = std::move(complete);
                } else {
                    complete(true);
                }
            }
        },
        [weak](const BridgeCloseReason reason) {
            if (const auto current = weak.lock()) {
                current->closed.push_back(reason);
                if (const auto owner = current->bridge.lock()) {
                    owner->Stop(); // shutdown from notification must be idempotent
                }
            }
        },
        options);
}

TEST(RdpPacket, ExistingEnvelopePreservesEveryByte) {
    std::string content(kMaxPayloadBytes, '\0');
    for (std::size_t index = 0; index < content.size(); ++index) {
        content[index] = static_cast<char>(index % 256);
    }
    const auto wire = EncodeData(Binding(), content);
    ASSERT_TRUE(wire);
    px::Message envelope{};
    ASSERT_TRUE(envelope.ParseFromString(wire->AsString()));
    EXPECT_EQ(envelope.type(), px::kRdpStream);
    const auto decoded = DecodePacket(Binding(), wire->Bytes());
    ASSERT_EQ(decoded.status, PacketStatus::kData);
    ASSERT_TRUE(decoded.payload);
    EXPECT_EQ(decoded.payload->AsString(), content);
}

TEST(RdpPacket, InvalidLimitsAndBindingsAreRejected) {
    EXPECT_FALSE(EncodeData({}, std::string("x")));
    EXPECT_FALSE(EncodeData(Binding(), {}));
    EXPECT_FALSE(EncodeData(Binding(), std::string(kMaxPayloadBytes + 1, 'x')));
    EXPECT_FALSE(EncodeClose({.connection_id = std::string(129, 'x'), .generation = 1}));
    EXPECT_FALSE(EncodeClose({.connection_id = "x", .generation = 0}));
    EXPECT_EQ(DecodePacket(Binding(), {}).status, PacketStatus::kInvalid);
    EXPECT_EQ(DecodePacket(Binding(), std::string(kMaxWireBytes + 1, 'x')).status, PacketStatus::kInvalid);
    EXPECT_EQ(DecodePacket(Binding(), std::string("not protobuf")).status, PacketStatus::kInvalid);
}

TEST(RdpPacket, CloseAndStaleGenerationHaveDifferentSemantics) {
    const auto close = EncodeClose(Binding());
    ASSERT_TRUE(close);
    EXPECT_EQ(DecodePacket(Binding(), close->Bytes()).status, PacketStatus::kClose);
    auto next = Binding();
    ++next.generation;
    EXPECT_EQ(DecodePacket(next, close->Bytes()).status, PacketStatus::kStaleBinding);
    next = Binding();
    next.connection_id = "other-connection";
    EXPECT_EQ(DecodePacket(next, close->Bytes()).status, PacketStatus::kStaleBinding);
}

TEST(RdpPacket, OtherMessagesAndMalformedRdpAreNotData) {
    px::Message envelope{};
    envelope.set_type(px::kHeartBeat);
    EXPECT_EQ(DecodePacket(Binding(), envelope.SerializeAsString()).status, PacketStatus::kOtherMessage);
    envelope.set_type(px::kRdpStream);
    EXPECT_EQ(DecodePacket(Binding(), envelope.SerializeAsString()).status, PacketStatus::kInvalid);
    auto& packet = *envelope.mutable_rdp_stream(); // Protobuf-owned transient ABI boundary.
    packet.set_version(1);
    packet.set_connection_id(Binding().connection_id);
    packet.set_generation(Binding().generation);
    packet.set_kind(px::RdpStreamPacket::CLOSE);
    packet.set_payload("illegal close payload");
    EXPECT_EQ(DecodePacket(Binding(), envelope.SerializeAsString()).status, PacketStatus::kInvalid);
    packet.clear_payload();
    packet.set_version(2);
    EXPECT_EQ(DecodePacket(Binding(), envelope.SerializeAsString()).status, PacketStatus::kInvalid);
}

TEST(RdpBridge, FactoryRejectsInvalidConfiguration) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    EXPECT_FALSE(MakeBridge(io, observer, {.max_pending_bytes = 1}));
    EXPECT_FALSE(MakeBridge(io, observer, {.connect_timeout = 0ms}));
    EXPECT_FALSE(MakeBridge(io, observer, {.send_timeout = 0ms}));
    EXPECT_FALSE(RdpTcpBridge::Create(io->get_executor(), Binding(), {}, [](BridgeCloseReason) {}));
}

TEST(RdpBridge, BinaryFragmentsArriveInOrderWithoutEnvelopeBytes) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    const auto sockets = ConnectPair(io);
    const auto bridge = MakeBridge(io, observer);
    bridge->Attach(sockets.local);
    EXPECT_TRUE(bridge->Receive(EncodeData(Binding(), std::string("one\0", 4))));
    EXPECT_TRUE(bridge->Receive(EncodeData(Binding(), std::string("two\xff", 4))));
    ASSERT_TRUE(PumpUntil(io, [bridge, peer = sockets.peer] { return bridge->PendingBytes() == 0 && peer->available() == 8; }));
    EXPECT_EQ(ReadAvailable(sockets.peer), std::string("one\0two\xff", 8));
    EXPECT_EQ(bridge->PendingBytes(), 0);
    bridge->Stop();
    Pump(io);
    ASSERT_EQ(observer->closed.size(), 1);
    EXPECT_EQ(observer->closed.front(), BridgeCloseReason::kStopped);
}

TEST(RdpBridge, OutboundReadWaitsForWebSocketWriteCompletion) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    observer->delay_send = true;
    const auto sockets = ConnectPair(io);
    const auto bridge = MakeBridge(io, observer);
    bridge->Attach(sockets.local);
    asio::write(*sockets.peer, asio::buffer(std::string("first")));
    ASSERT_TRUE(PumpUntil(io, [observer] { return !observer->packets.empty(); }));
    ASSERT_EQ(observer->packets.size(), 1);
    EXPECT_EQ(observer->packets.front().payload->AsString(), "first");
    asio::write(*sockets.peer, asio::buffer(std::string("second")));
    Pump(io);
    EXPECT_EQ(observer->packets.size(), 1);
    ASSERT_TRUE(observer->delayed);
    const auto completed = std::move(observer->delayed);
    completed(true);
    completed(true); // duplicate completion cannot start a second read
    ASSERT_TRUE(PumpUntil(io, [observer] { return observer->packets.size() == 2; }));
    ASSERT_EQ(observer->packets.size(), 2);
    EXPECT_EQ(observer->packets.back().payload->AsString(), "second");
    bridge->Stop();
    Pump(io);
}

TEST(RdpBridge, QueuedIngressIsBoundedBeforeExecutorRuns) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    const auto bridge = MakeBridge(io, observer, {.max_pending_bytes = kMaxWireBytes});
    const auto wire = EncodeData(Binding(), std::string(kMaxPayloadBytes, 'x'));
    EXPECT_TRUE(bridge->Receive(wire));
    EXPECT_FALSE(bridge->Receive(wire));
    EXPECT_LE(bridge->PendingBytes(), kMaxWireBytes);
    Pump(io);
    EXPECT_EQ(bridge->PendingBytes(), 0);
    ASSERT_EQ(observer->closed.size(), 1);
    EXPECT_EQ(observer->closed.front(), BridgeCloseReason::kQueueFull);
}

TEST(RdpBridge, StaleCloseCannotStopNewBinding) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    const auto bridge = MakeBridge(io, observer);
    auto old = Binding();
    --old.generation;
    EXPECT_TRUE(bridge->Receive(EncodeClose(old)));
    Pump(io);
    EXPECT_TRUE(observer->closed.empty());
    EXPECT_TRUE(bridge->Receive(EncodeClose(Binding())));
    Pump(io);
    ASSERT_EQ(observer->closed.size(), 1);
    EXPECT_EQ(observer->closed.front(), BridgeCloseReason::kPeerClosed);
    EXPECT_TRUE(observer->packets.empty()); // do not echo CLOSE forever
}

TEST(RdpBridge, DestroyWithPendingReadAndLateCompletionIsSafe) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    observer->delay_send = true;
    const auto sockets = ConnectPair(io);
    auto bridge = MakeBridge(io, observer);
    const auto weak = std::weak_ptr<RdpTcpBridge>(bridge);
    bridge->Attach(sockets.local);
    asio::write(*sockets.peer, asio::buffer(std::string("late")));
    ASSERT_TRUE(PumpUntil(io, [observer] { return static_cast<bool>(observer->delayed); }));
    ASSERT_TRUE(observer->delayed);
    bridge.reset();
    EXPECT_TRUE(weak.expired());
    observer->delayed(true);
    Pump(io);
    EXPECT_FALSE(sockets.local->is_open());
    EXPECT_TRUE(observer->closed.empty());
}

TEST(RdpBridge, StopFromSendAndClosedCallbacksIsSafe) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    observer->stop_in_callback = true;
    const auto sockets = ConnectPair(io);
    const auto bridge = MakeBridge(io, observer);
    observer->bridge = bridge;
    bridge->Attach(sockets.local);
    asio::write(*sockets.peer, asio::buffer(std::string("stop")));
    ASSERT_TRUE(PumpUntil(io, [observer] { return !observer->closed.empty(); }));
    EXPECT_EQ(observer->closed.size(), 1);
    EXPECT_FALSE(sockets.local->is_open());
    EXPECT_FALSE(bridge->Receive(EncodeData(Binding(), std::string("after stop"))));
}

TEST(RdpBridge, MissingSendCompletionTimesOut) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    observer->delay_send = true;
    const auto sockets = ConnectPair(io);
    const auto bridge = MakeBridge(io, observer, {.send_timeout = 1ms});
    bridge->Attach(sockets.local);
    asio::write(*sockets.peer, asio::buffer(std::string("timeout")));
    // Windows timer granularity can exceed one 10 ms pump; wait for the observable event, with a hard test deadline.
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (observer->closed.empty() && std::chrono::steady_clock::now() < deadline) {
        Pump(io);
    }
    ASSERT_EQ(observer->closed.size(), 1);
    EXPECT_EQ(observer->closed.front(), BridgeCloseReason::kTimedOut);
    observer->delayed(true);
    Pump(io);
    EXPECT_EQ(observer->closed.size(), 1);
}

TEST(RdpBridge, RepeatedStopAndQueuedDestructionDoNotLeakOwners) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    for (int iteration = 0; iteration < 100; ++iteration) {
        auto bridge = MakeBridge(io, observer);
        const auto weak = std::weak_ptr<RdpTcpBridge>(bridge);
        EXPECT_TRUE(bridge->Receive(EncodeData(Binding(), std::string("queued"))));
        bridge->Stop();
        bridge->Stop();
        bridge.reset();
        EXPECT_TRUE(weak.expired());
        Pump(io);
    }
    EXPECT_TRUE(observer->closed.empty());
}

TEST(RdpBridge, FailedSendClosesInsteadOfSilentlyDroppingBytes) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    observer->delay_send = true;
    const auto sockets = ConnectPair(io);
    const auto bridge = MakeBridge(io, observer);
    bridge->Attach(sockets.local);
    asio::write(*sockets.peer, asio::buffer(std::string("failure")));
    ASSERT_TRUE(PumpUntil(io, [observer] { return static_cast<bool>(observer->delayed); }));
    ASSERT_TRUE(observer->delayed);
    observer->delayed(false);
    Pump(io);
    ASSERT_EQ(observer->closed.size(), 1);
    EXPECT_EQ(observer->closed.front(), BridgeCloseReason::kSendFailed);
}

TEST(RdpBridge, LoopbackConnectFlushesDataQueuedBeforeConnect) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    asio::ip::tcp::acceptor acceptor(*io, {asio::ip::address_v4::loopback(), 0});
    const auto peer = std::make_shared<asio::ip::tcp::socket>(*io);
    const auto accepted = std::make_shared<bool>(false);
    acceptor.async_accept(*peer, [accepted](const asio::error_code& error) { *accepted = !error; });
    const auto bridge = MakeBridge(io, observer);
    ASSERT_TRUE(bridge->Receive(EncodeData(Binding(), std::string("before connect"))));
    bridge->ConnectLoopback(acceptor.local_endpoint().port());
    ASSERT_TRUE(PumpUntil(io, [accepted, bridge, peer] { return *accepted && bridge->PendingBytes() == 0 && peer->available() == 14; }));
    ASSERT_TRUE(*accepted);
    EXPECT_TRUE(peer->remote_endpoint().address().is_loopback());
    EXPECT_EQ(ReadAvailable(peer), "before connect");
    EXPECT_EQ(bridge->PendingBytes(), 0);
    bridge->Stop();
    Pump(io);
    ASSERT_EQ(observer->closed.size(), 1);
}

TEST(RdpBridge, InvalidLoopbackPortClosesOnceAndReleasesQueue) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    const auto bridge = MakeBridge(io, observer);
    ASSERT_TRUE(bridge->Receive(EncodeData(Binding(), std::string("queued"))));
    bridge->ConnectLoopback(0);
    Pump(io);
    ASSERT_EQ(observer->closed.size(), 1);
    EXPECT_EQ(observer->closed.front(), BridgeCloseReason::kConnectFailed);
    EXPECT_EQ(bridge->PendingBytes(), 0);
    bridge->Stop();
    Pump(io);
    EXPECT_EQ(observer->closed.size(), 1);
}

TEST(RdpBridge, DestroyWithOutstandingTcpReadClosesSocket) {
    const auto io = std::make_shared<asio::io_context>();
    const auto observer = std::make_shared<Observer>();
    const auto sockets = ConnectPair(io);
    auto bridge = MakeBridge(io, observer);
    bridge->Attach(sockets.local);
    Pump(io);
    const auto weak = std::weak_ptr<RdpTcpBridge>(bridge);
    bridge.reset();
    EXPECT_TRUE(weak.expired());
    Pump(io);
    EXPECT_FALSE(sockets.local->is_open());
    EXPECT_TRUE(observer->closed.empty());
}

TEST(ReliableWrite, CompletionReportsActualOutcomeOnlyOnce) {
    const auto outcomes = std::make_shared<std::vector<bool>>();
    {
        const auto completion = std::make_shared<px::ReliableWriteCompletion>([outcomes](bool ok) { outcomes->push_back(ok); });
        completion->Complete(true);
        completion->Complete(false);
    }
    ASSERT_EQ(outcomes->size(), 1);
    EXPECT_TRUE(outcomes->front());
}

TEST(ReliableWrite, DiscardedQueueCompletionReportsFailure) {
    const auto outcomes = std::make_shared<std::vector<bool>>();
    {
        const auto completion = std::make_shared<px::ReliableWriteCompletion>([outcomes](bool ok) { outcomes->push_back(ok); });
    }
    ASSERT_EQ(outcomes->size(), 1);
    EXPECT_FALSE(outcomes->front());
}

TEST(ReliableWrite, CallbackMayThrowAndDestroyItsLastExternalOwner) {
    const auto slot = std::make_shared<std::shared_ptr<px::ReliableWriteCompletion>>();
    const auto weak_slot = std::weak_ptr<std::shared_ptr<px::ReliableWriteCompletion>>(slot);
    *slot = std::make_shared<px::ReliableWriteCompletion>([weak_slot](bool) {
        if (const auto owner = weak_slot.lock()) {
            owner->reset();
        }
        throw std::runtime_error("test callback failure");
    });
    const auto completion = *slot;
    EXPECT_NO_THROW(completion->Complete(true));
    EXPECT_FALSE(*slot);
}

} // namespace
