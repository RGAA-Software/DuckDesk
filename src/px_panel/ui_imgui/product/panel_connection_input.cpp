#include "panel_connection_input.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <nlohmann/json.hpp>
#include <string_view>
#include <system_error>

#include "px_common/base64.h"

namespace px::panel::product {
namespace {

std::string Trim(std::string value) {
    const auto whitespace = [](const unsigned char character) { return std::isspace(character) != 0; };
    value.erase(value.begin(), std::find_if_not(value.begin(), value.end(), whitespace));
    value.erase(std::find_if_not(value.rbegin(), value.rend(), whitespace).base(), value.end());
    return value;
}

bool IsDeviceId(const std::string& value) {
    return !value.empty() && std::ranges::all_of(value, [](const unsigned char character) { return std::isdigit(character) != 0; });
}

std::optional<int> ParsePort(const std::string_view value) {
    int port{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), port);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() && port > 0 && port <= 65535 ? std::optional{port} : std::nullopt;
}

std::optional<ParsedConnectionInput> ParseSharedLink(const std::string& value) {
    try {
        const auto payload = nlohmann::json::parse(Base64::Base64Decode(value.substr(std::string_view{"link://"}.size())));
        ParsedConnectionInput result{.kind = ConnectionInputKind::SharedLink,
                                     .deviceId = payload.value("did", ""),
                                     .displayName = payload.value("dn", ""),
                                     .port = payload.value("rdpt", 0),
                                     .password = payload.value("rpwd", "")};
        if (const auto addresses = payload.find("ips"); addresses != payload.end() && addresses->is_array()) {
            for (const auto& address : *addresses) {
                if (address.is_object()) {
                    const auto host = address.value("ip", "");
                    if (!host.empty()) {
                        result.hosts.push_back(host);
                    }
                }
            }
        }
        return result.port > 0 && result.port <= 65535 && !result.password.empty() && !result.hosts.empty() ? std::optional{std::move(result)}
                                                                                                            : std::nullopt;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::optional<ParsedConnectionInput> ParseDirectEndpoint(std::string value, const int defaultPort) {
    if (const auto scheme = value.find("://"); scheme != std::string::npos) {
        value.erase(0, scheme + 3);
    }
    if (const auto path = value.find_first_of("/?#"); path != std::string::npos) {
        value.erase(path);
    }
    std::string host{};
    int port{defaultPort};
    if (value.starts_with('[')) {
        const auto closing = value.find(']');
        if (closing == std::string::npos) {
            return std::nullopt;
        }
        host = value.substr(1, closing - 1);
        if (closing + 1 < value.size()) {
            if (value[closing + 1] != ':') {
                return std::nullopt;
            }
            const auto parsedPort = ParsePort(std::string_view{value}.substr(closing + 2));
            if (!parsedPort) {
                return std::nullopt;
            }
            port = *parsedPort;
        }
    } else if (std::ranges::count(value, ':') == 1) {
        const auto separator = value.rfind(':');
        host = value.substr(0, separator);
        const auto parsedPort = ParsePort(std::string_view{value}.substr(separator + 1));
        if (!parsedPort) {
            return std::nullopt;
        }
        port = *parsedPort;
    } else {
        host = std::move(value);
    }
    host = Trim(std::move(host));
    return !host.empty() && port > 0 && port <= 65535
               ? std::optional{ParsedConnectionInput{.kind = ConnectionInputKind::DirectEndpoint, .displayName = host, .hosts = {host}, .port = port}}
               : std::nullopt;
}

} // namespace

std::optional<ParsedConnectionInput> ParseConnectionInput(std::string value, const int defaultPort) {
    value = Trim(std::move(value));
    if (value.empty()) {
        return std::nullopt;
    }
    if (value.starts_with("link://")) {
        return ParseSharedLink(value);
    }
    if (IsDeviceId(value)) {
        return ParsedConnectionInput{.kind = ConnectionInputKind::DeviceId, .deviceId = std::move(value)};
    }
    return ParseDirectEndpoint(std::move(value), defaultPort);
}

bool ConnectionInputNeedsPassword(const std::string& value) {
    const auto parsed = ParseConnectionInput(value, 4601);
    return parsed && parsed->kind != ConnectionInputKind::SharedLink;
}

} // namespace px::panel::product
