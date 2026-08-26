//
// Created by RGAA on 2024-04-20.
//

#ifndef PX_STATISTICS_H
#define PX_STATISTICS_H

#include <map>
#include <memory>
#include <vector>
#include <cstdint>
#include <string>
#include "px_app_messages.h"
#include "px_render_panel_message.pb.h"
#include "px_common_new/concurrent_type.h"
#include "px_common_new/concurrent_vector.h"
#include "px_common_new/concurrent_hashmap.h"

namespace px
{

    class PxContext;
    class MessageListener;

    // relay alive
    class PxStatRelayAlive {
    public:
        std::string device_id_;
        int64_t created_ts_ = 0;
        int64_t last_update_ts_ = 0;
    };

    class PxStatistics : public std::enable_shared_from_this<PxStatistics> {
    public:

        static std::shared_ptr<PxStatistics> Instance() {
            static auto instance = std::make_shared<PxStatistics>();
            return instance;
        }

        ~PxStatistics();
        void SetContext(const std::shared_ptr<PxContext>& ctx) { context_ = ctx; }
        void RegisterEventListeners();
        void Exit();

        std::map<std::string, std::vector<int32_t>> GetEncodeDurations();
        std::map<std::string, std::vector<int32_t>> GetVideoCaptureGaps();
        std::map<std::string, std::vector<int32_t>> GetCopyTextureDurations();
        std::map<std::string, std::vector<int32_t>> GetMapCvtTextureDurations();
        std::vector<int32_t> GetAudioFrameGaps();
        std::vector<double> GetLeftSpectrum();
        std::vector<double> GetRightSpectrum();
        std::vector<std::shared_ptr<pxrp::RpMsgWorkingCaptureInfo>> GetCapturesInfo();
        std::vector<std::shared_ptr<pxrp::RpConnectedClientInfo>> GetConnectedClientsInfo();
        void UpdateRelayAlive(const std::string& device_id, int64_t timestamp);
        int64_t GetRelayLastUpdateTimestamp(const std::string& device_id);

    private:
        void ProcessCaptureStatistics(const MsgCaptureStatistics& msg);
        void ProcessAudioSpectrum(const MsgServerAudioSpectrum& msg);
        void Process1SCalculation();

    private:
        ConcurrentHashMap<std::string, std::vector<int32_t>> encode_durations_;
        ConcurrentHashMap<std::string, std::vector<int32_t>> video_capture_gaps_;
        ConcurrentHashMap<std::string, std::vector<int32_t>> copy_texture_durations_;
        ConcurrentHashMap<std::string, std::vector<int32_t>> map_cvt_texture_durations_;
        ConcurrentVector<uint32_t> audio_frame_gaps_;
        ConcurrentVector<double> left_spectrum_;
        ConcurrentVector<double> right_spectrum_;
        ConcurrentVector<std::shared_ptr<pxrp::RpMsgWorkingCaptureInfo>> captures_info_;
        ConcurrentVector<std::shared_ptr<pxrp::RpConnectedClientInfo>> connected_clients_info_;
        ConcurrentHashMap<std::string, std::shared_ptr<PxStatRelayAlive>> relays_alive_;

    public:
        std::weak_ptr<PxContext> context_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        // from inner server
        std::atomic_int32_t app_running_time = 0;
        // from inner server
        std::atomic_int64_t server_send_media_bytes = 0;
        std::atomic_int64_t last_server_send_media_bytes = 0;
        std::atomic_int64_t send_speed_bytes = 0;

        // from inner server
        std::atomic_int32_t audio_samples_{0};
        std::atomic_int32_t audio_channels_{0};
        std::atomic_int32_t audio_bits_{0};

        std::atomic_int32_t connected_clients_ = 0;
        ConcurrentString video_capture_type_;
        ConcurrentString video_encode_type_;
        std::atomic_bool relay_connected_ = false;
        ConcurrentString audio_capture_type_;

    };

}

#endif //PX_STATISTICS_H
