#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace px {

inline constexpr std::uint32_t kCaptureTextInputCommand = 0x0008;
inline constexpr std::uint32_t kCaptureTextInputReply = 0x0009;
inline constexpr std::uint32_t kCaptureTextInputMagic = 0x47525458;
inline constexpr std::size_t kCaptureTextInputMaxBytes = 16 * 1024;

enum class CaptureTextOperation : std::uint32_t { kQuery = 1, kSubmit = 2, kRelease = 3 };
enum class CaptureTextStatus : std::uint32_t { kReady, kSubmitted, kTargetChanged, kUnavailable, kInvalidText, kOutcomeUnknown, kBusy };
enum class CaptureTextEditability : std::uint32_t { kUnknown, kEditable, kNotEditable };

struct CaptureTextCommand final {
    CaptureTextOperation operation{CaptureTextOperation::kQuery};
    std::uint64_t request_id{};
    std::uint64_t expected_generation{};
    std::uint64_t root_window{}; // Internal IPC only: selected by the authenticated Render, never accepted from a frontend.
    std::uint32_t target_pid{};
    std::string text{};
};

struct CaptureTextReply final {
    std::uint64_t request_id{};
    std::uint64_t generation{};
    CaptureTextStatus status{CaptureTextStatus::kUnavailable};
    CaptureTextEditability editability{CaptureTextEditability::kUnknown};
    std::uint32_t accepted_bytes{};
};

namespace capture_text_wire {
inline void Append(std::string& output, std::uint64_t value, std::size_t bytes) {
    for (std::size_t index{}; index < bytes; ++index) {
        output.push_back(static_cast<char>((value >> (index * 8)) & 0xff));
    }
}
inline std::uint64_t Read(std::string_view input, std::size_t offset, std::size_t bytes) {
    std::uint64_t value{};
    for (std::size_t index{}; index < bytes; ++index) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(input[offset + index])) << (index * 8);
    }
    return value;
}
inline bool Header(std::string_view input, std::uint32_t type, std::size_t minimum) {
    return input.size() >= minimum && Read(input, 0, 4) == type && Read(input, 4, 4) == kCaptureTextInputMagic && Read(input, 8, 4) == 1;
}
} // namespace capture_text_wire

inline std::string EncodeCaptureTextCommand(const CaptureTextCommand& command) {
    if (command.text.size() > kCaptureTextInputMaxBytes) {
        return {};
    }
    std::string output{};
    for (const auto value : {kCaptureTextInputCommand, kCaptureTextInputMagic, 1U, static_cast<std::uint32_t>(command.operation)}) {
        capture_text_wire::Append(output, value, 4);
    }
    capture_text_wire::Append(output, command.request_id, 8);
    capture_text_wire::Append(output, command.expected_generation, 8);
    capture_text_wire::Append(output, command.root_window, 8);
    capture_text_wire::Append(output, command.target_pid, 4);
    capture_text_wire::Append(output, command.text.size(), 4);
    output += command.text;
    return output;
}

inline std::optional<CaptureTextCommand> DecodeCaptureTextCommand(std::string_view input) {
    if (!capture_text_wire::Header(input, kCaptureTextInputCommand, 48)) {
        return std::nullopt;
    }
    const auto operation = capture_text_wire::Read(input, 12, 4);
    const auto length = capture_text_wire::Read(input, 44, 4);
    if ((operation < 1 || operation > 3) || length > kCaptureTextInputMaxBytes || input.size() != 48 + length) {
        return std::nullopt;
    }
    return CaptureTextCommand{static_cast<CaptureTextOperation>(operation),
                              capture_text_wire::Read(input, 16, 8),
                              capture_text_wire::Read(input, 24, 8),
                              capture_text_wire::Read(input, 32, 8),
                              static_cast<std::uint32_t>(capture_text_wire::Read(input, 40, 4)),
                              std::string(input.substr(48))};
}

inline std::string EncodeCaptureTextReply(const CaptureTextReply& reply) {
    std::string output{};
    for (const auto value : {kCaptureTextInputReply, kCaptureTextInputMagic, 1U, static_cast<std::uint32_t>(reply.status)}) {
        capture_text_wire::Append(output, value, 4);
    }
    capture_text_wire::Append(output, reply.request_id, 8);
    capture_text_wire::Append(output, reply.generation, 8);
    capture_text_wire::Append(output, static_cast<std::uint32_t>(reply.editability), 4);
    capture_text_wire::Append(output, reply.accepted_bytes, 4);
    return output;
}

inline std::optional<CaptureTextReply> DecodeCaptureTextReply(std::string_view input) {
    if (input.size() != 40 || !capture_text_wire::Header(input, kCaptureTextInputReply, 40)) {
        return std::nullopt;
    }
    const auto status = capture_text_wire::Read(input, 12, 4);
    const auto editable = capture_text_wire::Read(input, 32, 4);
    if (status > static_cast<std::uint32_t>(CaptureTextStatus::kBusy) || editable > 2 ||
        capture_text_wire::Read(input, 36, 4) > kCaptureTextInputMaxBytes) {
        return std::nullopt;
    }
    return CaptureTextReply{capture_text_wire::Read(input, 16, 8), capture_text_wire::Read(input, 24, 8), static_cast<CaptureTextStatus>(status),
                            static_cast<CaptureTextEditability>(editable), static_cast<std::uint32_t>(capture_text_wire::Read(input, 36, 4))};
}
} // namespace px
