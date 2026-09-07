#include "sdk_stream_helper.h"

#include <cstdint>
#include <optional>

namespace px {
namespace {

struct StartCode final {
    std::size_t offset{};
    std::size_t size{};
};

std::optional<StartCode> FindStartCode(const std::string_view frame, const std::size_t from) {
    for (auto index = from; index + 3U <= frame.size(); ++index) {
        if (frame[index] != '\0' || frame[index + 1U] != '\0') continue;
        if (frame[index + 2U] == '\1') return StartCode{index, 3U};
        if (index + 4U <= frame.size() && frame[index + 2U] == '\0' && frame[index + 3U] == '\1') {
            return StartCode{index, 4U};
        }
    }
    return std::nullopt;
}

template <typename Action>
void ForEachAnnexBNalUnit(const std::string_view frame, Action action) {
    auto start = FindStartCode(frame, 0U);
    while (start) {
        const auto header = start->offset + start->size;
        if (header >= frame.size()) return;
        const auto next = FindStartCode(frame, header + 1U);
        const auto end = next ? next->offset : frame.size();
        action(frame.substr(start->offset, end - start->offset), static_cast<std::uint8_t>(frame[header]));
        start = next;
    }
}

} // namespace

H264ParameterSets StreamHelper::ExtractH264ParameterSets(const std::string_view frame) {
    H264ParameterSets result{};
    ForEachAnnexBNalUnit(frame, [&result](const std::string_view nal, const std::uint8_t header) {
        switch (header & 0x1FU) {
        case 7U:
            result.sps.assign(nal);
            break;
        case 8U:
            result.pps.assign(nal);
            break;
        default:
            break;
        }
    });
    return result;
}

std::string StreamHelper::ExtractH265ParameterSets(const std::string_view frame) {
    std::string result{};
    ForEachAnnexBNalUnit(frame, [&result](const std::string_view nal, const std::uint8_t header) {
        const auto type = static_cast<std::uint8_t>((header >> 1U) & 0x3FU);
        if (type == 32U || type == 33U || type == 34U) result.append(nal);
    });
    return result;
}

} // namespace px
