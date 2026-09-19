#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace px {

struct ConsoleFrontendRelayCredential final {
    std::int64_t revision{};
    std::string token{};
};

inline constexpr std::string_view kConsoleFrontendRelayCredentialPrefix{"px-console-frontend-v1|"};

[[nodiscard]] inline std::string BuildConsoleFrontendRelayCredential(const std::int64_t revision, const std::string_view token) {
    if (revision <= 0 || token.empty()) {
        return {};
    }
    return std::string{kConsoleFrontendRelayCredentialPrefix} + std::to_string(revision) + "|" + std::string{token};
}

[[nodiscard]] inline std::optional<ConsoleFrontendRelayCredential> ParseConsoleFrontendRelayCredential(const std::string_view encoded) {
    if (!encoded.starts_with(kConsoleFrontendRelayCredentialPrefix) || encoded.size() > 8'192U) {
        return std::nullopt;
    }
    const auto revision_offset = kConsoleFrontendRelayCredentialPrefix.size();
    const auto separator_offset = encoded.find('|', revision_offset);
    if (separator_offset == std::string_view::npos || separator_offset + 1U >= encoded.size()) {
        return std::nullopt;
    }
    std::int64_t revision{};
    const auto revision_begin = encoded.data() + revision_offset;
    const auto revision_end = encoded.data() + separator_offset;
    const auto parsed = std::from_chars(revision_begin, revision_end, revision);
    if (parsed.ec != std::errc{} || parsed.ptr != revision_end || revision <= 0) {
        return std::nullopt;
    }
    return ConsoleFrontendRelayCredential{.revision = revision, .token = std::string{encoded.substr(separator_offset + 1U)}};
}

}  // namespace px
