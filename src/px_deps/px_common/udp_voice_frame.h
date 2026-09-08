#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace px {

struct UdpVoiceFrame final {
    std::string association_code{};
    std::string call_id{};
    std::uint32_t sequence{};
    std::uint64_t capture_time_ms{};
    std::vector<std::uint8_t> opus{};
};

} // namespace px
