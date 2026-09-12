#include "panel_connection_links.h"

#include "px_common/base64.h"
#include "px_common/ip_util.h"

#include <charconv>
#include <cstdint>
#include <format>
#include <nlohmann/json.hpp>
#include <system_error>

namespace px::panel::product {
namespace {

int DeviceIconIndex(const std::string& deviceId) {
    std::uint64_t value{};
    const auto result = std::from_chars(deviceId.data(), deviceId.data() + deviceId.size(), value);
    return result.ec == std::errc{} ? static_cast<int>(value % 30U) + 1 : 1;
}

std::string UrlSafeBase64(std::string value) {
    for (auto& character : value) {
        if (character == '+') {
            character = '-';
        } else if (character == '/') {
            character = '_';
        }
    }
    while (!value.empty() && value.back() == '=') {
        value.pop_back();
    }
    return value;
}

} // namespace

std::vector<std::string> CollectPanelLocalAddresses() {
    std::vector<std::string> addresses{};
    for (const auto& adapter : IPUtil::ScanIPs()) {
        if (!adapter.ip_addr_.empty()) {
            addresses.push_back(adapter.ip_addr_);
        }
    }
    return addresses;
}

std::string ResolveNodeAccessHost(const std::string& configuredAddress, const std::vector<std::string>& localAddresses) {
    if (!configuredAddress.empty()) {
        return configuredAddress;
    }
    const auto address = std::ranges::find_if(localAddresses, [](const std::string& value) { return !value.empty() && value != "127.0.0.1"; });
    return address == localAddresses.end() ? std::string{} : *address;
}

PanelConnectionLinks BuildPanelConnectionLinks(const PanelIdentity& identity, const NodePorts& ports, const std::optional<ConsoleEndpoint>& console,
                                               const std::string& publicAddress, const std::vector<std::string>& localAddresses) {
    if (identity.deviceId.empty() || identity.randomPassword.empty()) {
        return {};
    }

    nlohmann::json addresses = nlohmann::json::array();
    if (!publicAddress.empty()) {
        addresses.push_back({{"ip", publicAddress}});
    } else {
        for (const auto& address : localAddresses) {
            if (!address.empty()) {
                addresses.push_back({{"ip", address}});
            }
        }
    }

    const nlohmann::json desktopPayload{{"did", identity.deviceId},
                                        {"dn", identity.deviceName},
                                        {"rpwd", identity.randomPassword},
                                        {"iidx", DeviceIconIndex(identity.deviceId)},
                                        {"ips", std::move(addresses)},
                                        {"ppt", ports.panel},
                                        {"rdpt", ports.desktop},
                                        {"rlst", console ? console->host : std::string{}},
                                        {"rlpt", console ? console->relayPort : 0},
                                        {"rlak", console ? console->appKey : std::string{}}};
    PanelConnectionLinks links{.desktop = "link://" + Base64::Base64Encode(desktopPayload.dump())};

    const std::string host{ResolveNodeAccessHost(publicAddress, localAddresses)};
    if (!host.empty()) {
        const nlohmann::json webPayload{{"d", identity.deviceId}, {"p", identity.randomPassword}};
        links.web = std::format("http://{}:{}/web/?c={}", host, ports.desktop, UrlSafeBase64(Base64::Base64Encode(webPayload.dump())));
    }
    return links;
}

} // namespace px::panel::product
