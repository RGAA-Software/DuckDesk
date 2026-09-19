#include "panel_node_presence.h"

#include "panel_config_store.h"
#include "panel_connection_links.h"

#include "console_panel.pb.h"
#include "px_common/base64.h"
#include "px_common/ip_util.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/time_util.h"
#include "px_common/uuid.h"

#include <asio2/websocket/wss_client.hpp>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include <array>
#include <chrono>
#include <format>
#include <iomanip>
#include <span>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace px::panel::product {
namespace {

struct ConnectionToken final {
    std::string token{};
    std::int64_t timestamp{};
    std::string nonce{};
};

std::string BytesToHex(const std::span<const unsigned char> bytes) {
    std::ostringstream output{};
    output << std::hex << std::setfill('0');
    for (const auto value : bytes) {
        output << std::setw(2) << static_cast<unsigned int>(value);
    }
    return output.str();
}

ConnectionToken GenerateConnectionToken(const std::string& appKey) {
    constexpr std::string_view salt{"bfa900206bed4db59156ae5fead1d249"};
    const std::string secretInput{appKey + std::string{salt}};
    std::array<unsigned char, SHA256_DIGEST_LENGTH> shaHash{};
    SHA256(reinterpret_cast<const unsigned char*>(secretInput.data()), secretInput.size(), shaHash.data());
    const std::string appSecret{MD5::Hex(BytesToHex(shaHash))};
    const std::int64_t timestamp{static_cast<std::int64_t>(TimeUtil::GetCurrentTimestamp())};
    const std::string nonce{GetUUID()};
    const std::string input{std::format("{}|{}|{}", appKey, timestamp, nonce)};
    std::array<unsigned char, EVP_MAX_MD_SIZE> hmac{};
    unsigned int hmacSize{};
    HMAC(EVP_sha256(), appSecret.data(), static_cast<int>(appSecret.size()), reinterpret_cast<const unsigned char*>(input.data()), input.size(),
         hmac.data(), &hmacSize);
    return {.token = BytesToHex(std::span{hmac}.first(hmacSize)), .timestamp = timestamp, .nonce = nonce};
}

bool IsUsableLanAddress(const std::string& address) {
    return !address.empty() && !address.starts_with("127.") && !address.starts_with("169.254.");
}

std::vector<std::string> LocalAddresses() {
    std::vector<std::string> result{};
    for (const auto& adapter : IPUtil::ScanIPs()) {
        if (IsUsableLanAddress(adapter.ip_addr_)) {
            result.push_back(adapter.ip_addr_);
        }
    }
    return result;
}

} // namespace

std::shared_ptr<PanelNodePresence> PanelNodePresence::Create(const std::shared_ptr<PanelConfigStore>& config) {
    auto result = std::make_shared<PanelNodePresence>(config);
    result->Start();
    return result;
}

PanelNodePresence::PanelNodePresence(std::shared_ptr<PanelConfigStore> config) : config_{std::move(config)} {}

PanelNodePresence::~PanelNodePresence() {
    Stop();
}

void PanelNodePresence::Start() {
    if (worker_.joinable()) {
        return;
    }
    stopping_ = false;
    const std::weak_ptr<PanelNodePresence> weakSelf{shared_from_this()};
    worker_ = std::jthread{[weakSelf](const std::stop_token stopToken) {
        if (const auto self = weakSelf.lock()) {
            self->Run(stopToken);
        }
    }};
}

void PanelNodePresence::Stop() {
    if (stopping_.exchange(true)) {
        return;
    }
    worker_.request_stop();
    if (worker_.joinable()) {
        worker_.join();
    }
    std::shared_ptr<asio2::wss_client> client{};
    {
        const std::scoped_lock lock{clientMutex_};
        client = std::move(client_);
    }
    if (client) {
        client->stop();
    }
    online_ = false;
    connecting_ = false;
}

bool PanelNodePresence::IsOnline() const noexcept {
    return online_.load(std::memory_order_acquire);
}

void PanelNodePresence::Run(const std::stop_token stopToken) {
    auto client = std::make_shared<asio2::wss_client>();
    ConfigureClient(client);
    {
        const std::scoped_lock lock{clientMutex_};
        client_ = client;
    }
    auto nextHeartbeat = std::chrono::steady_clock::now();
    while (!stopToken.stop_requested()) {
        const auto endpoint = config_->Console();
        const auto identity = config_->Identity();
        bool identityChanged{};
        {
            const std::scoped_lock lock{clientMutex_};
            identityChanged = !connectedDeviceId_.empty() && connectedDeviceId_ != identity.deviceId;
        }
        if (identityChanged) {
            client->stop();
            online_ = false;
            connecting_ = false;
            useLegacyPath_ = false;
            const std::scoped_lock lock{clientMutex_};
            connectedDeviceId_.clear();
        }
        if (endpoint && endpoint->IsValid() && !identity.deviceId.empty() && !connecting_.exchange(true)) {
            if (client->is_started()) {
                connecting_ = false;
            } else if (!client->async_start(endpoint->host, endpoint->port)) {
                connecting_ = false;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (online_ && now >= nextHeartbeat) {
            SendHeartbeat();
            nextHeartbeat = now + std::chrono::seconds{1};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds{250});
    }
}

void PanelNodePresence::ConfigureClient(const std::shared_ptr<asio2::wss_client>& client) {
    const std::weak_ptr<PanelNodePresence> weakSelf{shared_from_this()};
    const std::weak_ptr<asio2::wss_client> weakClient{client};
    client->set_auto_reconnect(false);
    client->keep_alive(true);
    client->set_timeout(std::chrono::milliseconds{3000});
    client->set_verify_mode(asio::ssl::verify_none);
    client->bind_init([weakSelf, weakClient] {
        const auto self = weakSelf.lock();
        const auto current = weakClient.lock();
        if (!self || !current || self->stopping_) {
            return;
        }
        const auto endpoint = self->config_->Console();
        const auto identity = self->config_->Identity();
        if (!endpoint) {
            return;
        }
        current->ws_stream().binary(true);
        current->set_no_delay(true);
        const auto token = GenerateConnectionToken(endpoint->appKey);
        const std::string_view route{self->useLegacyPath_ ? "/cms/panel" : "/console/panel"};
        current->set_upgrade_target(std::format("{}?appkey={}&token={}&ts={}&nonce={}&device_id={}&user_id=", route, endpoint->appKey, token.token,
                                                token.timestamp, token.nonce, identity.deviceId));
    });
    client->bind_connect([weakSelf] {
        if (const auto self = weakSelf.lock(); self && asio2::get_last_error()) {
            self->connecting_ = false;
            self->online_ = false;
        }
    });
    client->bind_upgrade([weakSelf] {
        const auto self = weakSelf.lock();
        if (!self) {
            return;
        }
        self->connecting_ = false;
        if (asio2::get_last_error()) {
            self->online_ = false;
            self->useLegacyPath_ = true;
            return;
        }
        self->online_ = true;
        {
            const std::scoped_lock lock{self->clientMutex_};
            self->connectedDeviceId_ = self->config_->Identity().deviceId;
        }
        self->SendHello();
    });
    client->bind_disconnect([weakSelf] {
        if (const auto self = weakSelf.lock()) {
            self->online_ = false;
            self->connecting_ = false;
            const std::scoped_lock lock{self->clientMutex_};
            self->connectedDeviceId_.clear();
        }
    });
    client->bind_recv([weakSelf](const std::string_view) {
        if (const auto self = weakSelf.lock()) {
            self->online_ = true;
        }
    });
}

void PanelNodePresence::SendHello() {
    const auto identity = config_->Identity();
    const auto addresses = LocalAddresses();
    console_panel::ConsolePanelMessage message{};
    message.set_msg_type(console_panel::ConsolePanelMessageType::kConsolePanelHello);
    auto& hello = *message.mutable_hello();
    hello.set_device_id(identity.deviceId);
    hello.set_device_name(identity.deviceName);
    hello.set_panel_http_port(config_->Ports().panel);
    for (const auto& address : addresses) {
        hello.add_panel_lan_ips(address);
    }
    Send(message.SerializeAsString());
}

void PanelNodePresence::SendHeartbeat() {
    const auto identity = config_->Identity();
    const auto addresses = LocalAddresses();
    const auto links = BuildPanelConnectionLinks(identity, config_->Ports(), config_->Console(), config_->NodePublicAddress(), addresses);
    console_panel::ConsolePanelMessage message{};
    message.set_msg_type(console_panel::ConsolePanelMessageType::kConsolePanelHeartBeat);
    auto& heartbeat = *message.mutable_heartbeat();
    heartbeat.set_hb_index(heartbeatIndex_.fetch_add(1));
    heartbeat.set_device_id(identity.deviceId);
    heartbeat.set_device_name(identity.deviceName);
    heartbeat.set_desktop_link(links.desktop);
    if (links.desktop.starts_with("link://")) {
        heartbeat.set_desktop_link_raw(Base64::Base64Decode(links.desktop.substr(7)));
    }
    if (!addresses.empty()) {
        heartbeat.set_device_ip_addr(addresses.front());
    }
    Send(message.SerializeAsString());
}

void PanelNodePresence::Send(const std::string& message) {
    std::shared_ptr<asio2::wss_client> client{};
    {
        const std::scoped_lock lock{clientMutex_};
        client = client_;
    }
    if (client && client->is_started() && online_) {
        client->async_send(message);
    }
}

} // namespace px::panel::product
