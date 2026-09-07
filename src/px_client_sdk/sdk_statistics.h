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
#include <memory>
#include "px_message.pb.h"
#include "px_common/fps_stat.h"
#include "px_common/concurrent_type.h"
#include "px_common/concurrent_vector.h"
#include "px_common/concurrent_hashmap.h"

namespace px
{

    constexpr auto kMaxStatCounts = 180;

    class SdkStatFrameSize {
    public:
        int width_ = 0;
        int height_ = 0;
    };

    class SdkStatistics {
    public:

        static std::shared_ptr<SdkStatistics> Instance() {
            static auto instance = std::make_shared<SdkStatistics>();
            return instance;
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
        px::ConcurrentHashMap<std::string, std::shared_ptr<FpsStat>> fps_video_recv_;
        px::ConcurrentHashMap<std::string, std::shared_ptr<FpsStat>> fps_render_;
        // in MB/S
        px::ConcurrentVector<float> recv_data_speeds_;
        // in MB/S
        px::ConcurrentVector<float> send_data_speeds_;
        // in ms
        px::ConcurrentVector<float> net_delays_;
        // monitor name <==> value
        px::ConcurrentHashMap<std::string, std::vector<float>> decode_durations_;
        px::ConcurrentHashMap<std::string, std::vector<float>> video_recv_gaps_;
        px::ConcurrentHashMap<std::string, std::vector<float>> video_recv_fps_;
        px::ConcurrentHashMap<std::string, SdkStatFrameSize> frames_size_;
        //
        px::ConcurrentHashMap<std::string, IsolatedMonitorStatisticsInfoInRender> render_monitor_stat_;

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
        ConcurrentType<px::RdHardwareInfo> remote_hd_info_;

        // render type [opengl / vulkan / d3d11] 
        ConcurrentString render_type_;

        // Full WebRTC transport diagnostics. These deliberately contain only
        // candidate type/address/protocol and aggregate metrics, never ICE
        // usernames, credentials or the Coturn REST secret.
        ConcurrentString rtc_ice_state_;
        ConcurrentString rtc_signaling_state_;
        ConcurrentString rtc_local_candidate_;
        ConcurrentString rtc_remote_candidate_;
        ConcurrentString rtc_turn_node_;
        std::atomic_int rtc_rtt_ms_ = 0;
        std::atomic_int64_t rtc_available_outgoing_bitrate_ = 0;
    };

}

#endif //GAMMARAYPC_STATISTICS_H
