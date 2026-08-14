//
// Created by RGAA on 2024-04-21.
//

#ifndef GAMMARAYPC_STATISTICS_H
#define GAMMARAYPC_STATISTICS_H

#include <map>
#include <vector>
#include <cstdint>
#include <string>
#include <atomic>
#include "tc_message.pb.h"
#include "tc_common_new/fps_stat.h"
#include "tc_common_new/concurrent_type.h"
#include "tc_common_new/concurrent_vector.h"
#include "tc_common_new/concurrent_hashmap.h"

namespace tc
{

    constexpr auto kMaxStatCounts = 180;

    class SdkStatFrameSize {
    public:
        int width_ = 0;
        int height_ = 0;
    };

    class SdkStatistics {
    public:

        static SdkStatistics* Instance() {
            static SdkStatistics instance;
            return &instance;
        }

        SdkStatistics();

        void AppendDecodeDuration(const std::string& monitor_name, int32_t time);
        void AppendVideoRecvGap(const std::string& monitor_name, int32_t time);
        void AppendRecvDataSize(int size);
        void AppendNetTimeDelay(int32_t delay);
        void AppendSentDataSize(int size);
        void TickVideoRecvFps(const std::string& monitor_name);
        void TickFrameRenderFps(const std::string& monitor_name);
        void UpdateFrameSize(const std::string& monitor_name, int width, int height);
        // By one second
        void CalculateDataSpeed();
        void CalculateVideoFrameFps();
        void UpdateIsolatedMonitorStatisticsInfoInRender(const std::string& mon_name, const IsolatedMonitorStatisticsInfoInRender& info);

        // Get
        std::vector<float> GetRecvDataSpeeds();
        std::vector<float> GetSendDataSpeeds();
        std::vector<float> GetNetDelays();
        std::map<std::string, std::vector<float>> GetDecodeDurations();
        std::map<std::string, std::vector<float>> GetVideoRecvGaps();
        std::map<std::string, std::vector<float>> GetVideoRecvFps();
        std::map<std::string, SdkStatFrameSize> GetFramesSize();
        std::map<std::string, IsolatedMonitorStatisticsInfoInRender> GetRenderMonitorsStat();

    private:
        tc::ConcurrentHashMap<std::string, std::shared_ptr<FpsStat>> fps_video_recv_;
        tc::ConcurrentHashMap<std::string, std::shared_ptr<FpsStat>> fps_render_;
        // in MB/S
        tc::ConcurrentVector<float> recv_data_speeds_;
        // in MB/S
        tc::ConcurrentVector<float> send_data_speeds_;
        // in ms
        tc::ConcurrentVector<float> net_delays_;
        // monitor name <==> value
        tc::ConcurrentHashMap<std::string, std::vector<float>> decode_durations_;
        tc::ConcurrentHashMap<std::string, std::vector<float>> video_recv_gaps_;
        tc::ConcurrentHashMap<std::string, std::vector<float>> video_recv_fps_;
        tc::ConcurrentHashMap<std::string, SdkStatFrameSize> frames_size_;
        //
        tc::ConcurrentHashMap<std::string, IsolatedMonitorStatisticsInfoInRender> render_monitor_stat_;

    public:
        // recv data
        std::atomic_int64_t recv_data_size_ = 0;
        std::atomic_int64_t last_recv_data_size_ = 0;

        // send data
        std::atomic_int64_t send_data_size_ = 0;
        std::atomic_int64_t last_send_data_size_ = 0;

        // h264 / hevc ...
        ConcurrentString video_format_;

        // 4:2:0 / 4:4:4
        ConcurrentString video_color_;

        //
        ConcurrentString video_decoder_;

        // DXGI / GDI
        ConcurrentString video_capture_type_;

        // WASAPI / Inner[HOOK]
        ConcurrentString audio_capture_type_;

        // OPUS / ...
        ConcurrentString audio_encode_type_;

        // remote pc info
        ConcurrentString remote_pc_info_;

        // remote desktop name
        ConcurrentString remote_desktop_name_;

        // remote os name
        ConcurrentString remote_os_name_;

        // remote detailed hardware information
        ConcurrentType<tc::RdHardwareInfo> remote_hd_info_;

        // render type [opengl / vulkan / d3d11] 
        ConcurrentString render_type_;
    };

}

#endif //GAMMARAYPC_STATISTICS_H
