//
// Created by RGAA on 10/08/2025.
//

#ifndef GAMMARAYPREMIUM_RTC_LOCAL_ENCODED_FRAME_H
#define GAMMARAYPREMIUM_RTC_LOCAL_ENCODED_FRAME_H

#include <string>
#include <memory>

namespace px
{

    class Data;

    class RtcLocalEncodedVideoFrame {
    public:
        std::string mon_name_;
        //PxPluginEncodedVideoType& video_type;
        int video_type_;
        std::shared_ptr<Data> data_ = nullptr;
        // 编码器产出序号(本插件按到达顺序编号,每屏独立):
        // NVENC 会跳帧编码,采集 frame_index 不连续,但本序号严格连续,
        // 消费端按它顺序取帧即可保证 H264 delta 链完整
        uint64_t seq_ = 0;
        uint64_t frame_index_ = 0;
        int frame_width_ = 0;
        int frame_height_ = 0;
        bool key_ = false;
        int64_t timestamp_ = 0;
    };

}

#endif //GAMMARAYPREMIUM_RTC_LOCAL_ENCODED_FRAME_H
