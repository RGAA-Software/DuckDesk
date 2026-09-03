#pragma once

#include <cstdint>
#include <string>

namespace px {

struct ClientModuleSettings final {
    bool clipboard_enabled_ = false;
    std::string device_id_;
    std::string stream_id_;
    int language_ = 1;
    std::string stream_name_;
    std::string display_name_;
    std::string display_remote_name_;
    std::uint64_t max_transmit_speed_ = 0;
    std::uint64_t max_receive_speed_ = 0;
};

struct ClientModuleConfig final {
    std::string screen_recording_path_;
    ClientModuleSettings settings_;
};

}  // namespace px
