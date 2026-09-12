#include "panel_device_name.h"

#include "px_common/ip_util.h"

#include <algorithm>
#include <charconv>
#include <string_view>

namespace px::panel::product {
namespace {

bool IsDecimal(const std::string_view value) {
    if (value.empty())
        return false;
    int parsed{};
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    return result.ec == std::errc{} && result.ptr == value.data() + value.size() && parsed >= 0 && parsed <= 255;
}

bool IsPrivateAddress(const std::string_view value) {
    if (value.starts_with("10.") || value.starts_with("192.168."))
        return true;
    if (!value.starts_with("172."))
        return false;
    const auto secondEnd = value.find('.', 4);
    if (secondEnd == std::string_view::npos)
        return false;
    int second{};
    const auto result = std::from_chars(value.data() + 4, value.data() + secondEnd, second);
    return result.ec == std::errc{} && second >= 16 && second <= 31;
}

std::string LastSegmentName(const std::string& address) {
    const auto separator = address.rfind('.');
    if (separator == std::string::npos || !IsDecimal(std::string_view{address}.substr(separator + 1)))
        return {};
    return "MC-" + address.substr(separator + 1);
}

} // namespace

std::string BuildDefaultDeviceName() {
    std::vector<std::string> addresses{};
    for (const auto& adapter : IPUtil::ScanIPs()) {
        if (!adapter.ip_addr_.empty())
            addresses.push_back(adapter.ip_addr_);
    }
    return BuildDefaultDeviceName(addresses);
}

std::string BuildDefaultDeviceName(const std::vector<std::string>& ipv4Addresses) {
    const auto privateAddress = std::ranges::find_if(ipv4Addresses, [](const std::string& value) { return IsPrivateAddress(value); });
    if (privateAddress != ipv4Addresses.end()) {
        if (auto name = LastSegmentName(*privateAddress); !name.empty())
            return name;
    }
    for (const auto& address : ipv4Addresses) {
        if (auto name = LastSegmentName(address); !name.empty() && address != "127.0.0.1")
            return name;
    }
    return "MC";
}

bool IsManagedDeviceName(const std::string& value) {
    constexpr std::string_view prefixes[]{"D-", "MC-", "Pixels Node"};
    return std::ranges::any_of(prefixes, [&value](const std::string_view prefix) {
        return value.starts_with(prefix) && IsDecimal(std::string_view{value}.substr(prefix.size()));
    });
}

} // namespace px::panel::product
