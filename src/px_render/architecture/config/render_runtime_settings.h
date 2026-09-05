#pragma once

#include <cstdint>
#include <string>

namespace px {

struct RenderRuntimeSettings final {
    std::string device_id;
    std::string device_random_password;
    std::string device_safety_password;
    std::string relay_host;
    std::string relay_port;
    bool can_be_operated{true};
    bool direct_allow_takeover{true};
    bool relay_enabled{true};
    int language{1};
    bool file_transfer_enabled{true};
    bool audio_enabled{true};
    std::string appkey;
    std::uint64_t max_transmit_speed{0};
    std::uint64_t max_receive_speed{0};
    int role{1};
};

} // namespace px
