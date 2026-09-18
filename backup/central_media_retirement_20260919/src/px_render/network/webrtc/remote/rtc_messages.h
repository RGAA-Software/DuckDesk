//
// Created by RGAA on 14/04/2025.
//

#ifndef PX_RTC_MESSAGES_H
#define PX_RTC_MESSAGES_H

#include <string>
#include <vector>

namespace px
{

    class MsgRtcRemoteSdp {
    public:
        std::string stream_id_;
        std::string device_id_;
        std::string sdp_;
        std::string ice_config_json_;
        std::vector<std::string> permissions_;
    };

    class MsgRtcRemoteIce {
    public:
        std::string stream_id_;
        std::string device_id_;
        std::string ice_;
        std::string mid_;
        int sdp_mline_index_ = 0;
    };

    bool IsRtcPayloadAuthorized(const std::string& payload,
                                const std::vector<std::string>& permissions);

}

#endif //PX_RTC_MESSAGES_H
