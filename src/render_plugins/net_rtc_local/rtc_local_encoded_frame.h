//
// Created by RGAA on 10/08/2025.
//

#ifndef GAMMARAYPREMIUM_RTC_LOCAL_ENCODED_FRAME_H
#define GAMMARAYPREMIUM_RTC_LOCAL_ENCODED_FRAME_H

#include <string>
#include <memory>

namespace tc
{

    class Data;

    class RtcLocalEncodedVideoFrame {
    public:
        std::string mon_name_;
        //GrPluginEncodedVideoType& video_type;
        int video_type_;
        std::shared_ptr<Data> data_ = nullptr;
        uint64_t frame_index_ = 0;
        int frame_width_ = 0;
        int frame_height_ = 0;
        bool key_ = false;
    };

}

#endif //GAMMARAYPREMIUM_RTC_LOCAL_ENCODED_FRAME_H
