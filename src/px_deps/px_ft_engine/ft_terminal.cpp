#include "ft_terminal.h"

#include <algorithm>
#include <cctype>
#include <string>

namespace px::ft {
namespace {

std::string AsciiLower(std::string_view value) {
    std::string result(value);
    std::ranges::transform(result, result.begin(), [](const unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return result;
}

bool Contains(std::string_view value, std::string_view needle) {
    return value.find(needle) != std::string_view::npos;
}

} // namespace

FtTerminalInfo ClassifyTerminal(std::string_view error_or_empty) {
    if (error_or_empty.empty()) {
        return {.status = "succeeded", .reason = "completed", .success = true};
    }

    const auto error = AsciiLower(error_or_empty);
    if (error == "cancel" || Contains(error, "cancelled") || Contains(error, "canceled")) {
        return {.status = "cancelled", .reason = "user_cancelled"};
    }
    if (error == "skipped" || Contains(error, "skip")) {
        return {.status = "skipped", .reason = "user_skipped"};
    }
    if (error == "interrupted") {
        return {.status = "aborted", .reason = "session_interrupted", .resumable = true};
    }
    if (Contains(error, "sha-256 mismatch")) {
        return {.status = "failed", .reason = "integrity_mismatch", .resumable = true};
    }
    if (Contains(error, "sha-256 is missing") ||
        Contains(error, "before sha-256 verification")) {
        return {.status = "failed", .reason = "integrity_hash_missing", .resumable = true};
    }
    if (Contains(error, "block sequence mismatch")) {
        return {.status = "failed", .reason = "block_sequence_mismatch", .resumable = true};
    }
    if (Contains(error, "no permission") || Contains(error, "permission denied") ||
        Contains(error, "access is denied")) {
        return {.status = "failed", .reason = "permission_denied"};
    }
    if (Contains(error, "too many files")) {
        return {.status = "failed", .reason = "file_count_limit"};
    }
    if (Contains(error, "path not exists") || Contains(error, "no such file")) {
        return {.status = "failed", .reason = "source_not_found"};
    }
    if (Contains(error, "failed to rename")) {
        return {.status = "failed", .reason = "destination_busy", .resumable = true};
    }
    if (Contains(error, "file write failed") || Contains(error, "failed to open file") ||
        Contains(error, "failed to create file") || Contains(error, "i/o error")) {
        return {.status = "failed", .reason = "io_error", .resumable = true};
    }
    if (Contains(error, "timeout") || Contains(error, "timed out")) {
        return {.status = "aborted", .reason = "transport_timeout", .resumable = true};
    }
    if (Contains(error, "disconnected") || Contains(error, "connection closed")) {
        return {.status = "aborted", .reason = "transport_disconnected", .resumable = true};
    }
    if (Contains(error, "route not found") || Contains(error, "route unavailable")) {
        return {.status = "aborted", .reason = "route_unavailable", .resumable = true};
    }
    if (Contains(error, "transport") || Contains(error, "send failed")) {
        return {.status = "aborted", .reason = "transport_error", .resumable = true};
    }
    return {.status = "failed", .reason = "transfer_failed", .resumable = true};
}

} // namespace px::ft
