#include "rtc_candidate_sdp.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <vector>

#include "px_common/log.h"

namespace px {
namespace {

bool EqualsAsciiInsensitive(const std::string_view left, const std::string_view right) {
    return left.size() == right.size() && std::equal(left.begin(), left.end(), right.begin(), right.end(), [](const char lhs, const char rhs) {
               return std::tolower(static_cast<unsigned char>(lhs)) == std::tolower(static_cast<unsigned char>(rhs));
           });
}

bool IsUsableIpv4(const std::string_view address) {
    std::istringstream input{std::string(address)};
    std::string segment;
    std::vector<unsigned long> parts;
    while (std::getline(input, segment, '.')) {
        if (segment.empty() || !std::ranges::all_of(segment, [](const char value) { return std::isdigit(static_cast<unsigned char>(value)) != 0; })) {
            return false;
        }
        try {
            std::size_t parsed{};
            const auto value = std::stoul(segment, &parsed);
            if (parsed != segment.size() || value > 255) {
                return false;
            }
            parts.push_back(value);
        } catch (const std::exception&) {
            return false;
        }
    }
    return parts.size() == 4 && parts[0] != 0 && parts[0] < 224 && !(parts[0] == 255 && parts[1] == 255 && parts[2] == 255 && parts[3] == 255);
}

std::optional<std::string> MakeAdvertisedCandidate(const std::string& line, const std::string& advertised_ipv4) {
    std::istringstream input{line};
    std::vector<std::string> fields;
    for (std::string field; input >> field;) {
        fields.push_back(std::move(field));
    }
    if (fields.size() < 8 || !fields[0].starts_with("a=candidate:") || !EqualsAsciiInsensitive(fields[2], "udp") ||
        !EqualsAsciiInsensitive(fields[6], "typ") || !EqualsAsciiInsensitive(fields[7], "host") || fields[4] == advertised_ipv4) {
        return std::nullopt;
    }
    try {
        std::size_t parsed{};
        const auto priority = std::stoul(fields[3], &parsed);
        if (parsed != fields[3].size() || priority >= std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        fields[3] = std::to_string(priority + 1);
    } catch (const std::exception&) {
        return std::nullopt;
    }
    constexpr std::size_t kMaxFoundationLength{32};
    constexpr std::string_view kCandidatePrefix{"a=candidate:"};
    constexpr std::string_view kPublicFoundationPrefix{"pxp"};
    const auto source_foundation = fields[0].substr(kCandidatePrefix.size());
    fields[0] = std::string(kCandidatePrefix) + std::string(kPublicFoundationPrefix) +
                source_foundation.substr(0, kMaxFoundationLength - kPublicFoundationPrefix.size());
    fields[4] = advertised_ipv4;
    std::ostringstream output;
    for (std::size_t index{}; index < fields.size(); ++index) {
        if (index != 0) {
            output << ' ';
        }
        output << fields[index];
    }
    return output.str();
}

} // namespace

std::string AddAdvertisedIpv4HostCandidates(const std::string& advertised_ipv4, const std::string& answer_sdp) {
    if (advertised_ipv4.empty()) {
        return answer_sdp;
    }
    if (!IsUsableIpv4(advertised_ipv4)) {
        LOGW("Ignoring invalid RTC advertised IPv4 address.");
        return answer_sdp;
    }

    std::istringstream input{answer_sdp};
    std::ostringstream output;
    std::size_t added{};
    for (std::string line; std::getline(input, line);) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        output << line << "\r\n";
        if (const auto candidate = MakeAdvertisedCandidate(line, advertised_ipv4)) {
            output << *candidate << "\r\n";
            ++added;
        }
    }
    if (added == 0) {
        return answer_sdp;
    }
    LOGI("Added {} RTC advertised UDP host candidates.", added);
    return output.str();
}

} // namespace px
