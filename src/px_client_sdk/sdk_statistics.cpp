//
// Created by RGAA on 2024-04-21.
//

#include "sdk_statistics.h"
#include "px_message.pb.h"
#include "px_common/log.h"
#include "px_common/num_formatter.h"

namespace px
{

    SdkStatistics::SdkStatistics() {

    }

    void SdkStatistics::AppendDecodeDuration(const std::string& monitor_name, int32_t time) {
        auto durations = decode_durations_.TryGet(monitor_name).value_or(std::vector<float>{});
        if (durations.size() >= kMaxStatCounts) {
            durations.erase(durations.begin());
        }
        durations.push_back((float)time);
        decode_durations_.Replace(monitor_name, durations);
    }

    std::map<std::string, std::vector<float>> SdkStatistics::GetDecodeDurations() {
        std::map<std::string, std::vector<float>> decode_durations;
        decode_durations_.ApplyAll([&](const std::string& monitor_name,
                                       const std::vector<float>&
                                           monitor_decode_durations) {
            decode_durations.insert({monitor_name, monitor_decode_durations});
        });
        return decode_durations;
    }

    void SdkStatistics::AppendVideoRecvGap(const std::string& monitor_name, int32_t time) {
        auto gaps = video_recv_gaps_.TryGet(monitor_name).value_or(std::vector<float>{});
        if (gaps.size() >= kMaxStatCounts) {
            gaps.erase(gaps.begin());
        }
        gaps.push_back((float)time);
        video_recv_gaps_.Replace(monitor_name, gaps);
    }

    std::map<std::string, std::vector<float>> SdkStatistics::GetVideoRecvGaps() {
        std::map<std::string, std::vector<float>> gaps;
        video_recv_gaps_.ApplyAll(
            [&](const std::string& monitor_name,
                const std::vector<float>& monitor_receive_gaps) {
                gaps.insert({monitor_name, monitor_receive_gaps});
            });
        return gaps;
    }

    void SdkStatistics::AppendRecvDataSize(int size) {
        recv_data_size_ += size;
    }

    void SdkStatistics::AppendSentDataSize(int size) {
        send_data_size_  += size;
    }

    void SdkStatistics::AppendNetTimeDelay(int32_t delay) {
        if (net_delays_.Size() >= kMaxStatCounts) {
            net_delays_.RemoveFirst();
        }
        net_delays_.PushBack(delay);
    }

    void SdkStatistics::TickVideoRecvFps(const std::string& monitor_name) {
        if (!fps_video_recv_.TryGet(monitor_name).has_value()) {
            fps_video_recv_.Insert(monitor_name, std::make_shared<FpsStat>());
        }
        if (auto fps_stat = fps_video_recv_.TryGet(monitor_name); fps_stat.has_value() && fps_stat.value()) {
            fps_stat.value()->Tick();
        }
    }

    void SdkStatistics::TickFrameRenderFps(const std::string& monitor_name) {
        if (!fps_render_.TryGet(monitor_name).has_value()) {
            fps_render_.Insert(monitor_name, std::make_shared<FpsStat>());
        }
        if (auto fps_stat = fps_render_.TryGet(monitor_name); fps_stat.has_value() && fps_stat.value()) {
            fps_stat.value()->Tick();
        }
    }

    void SdkStatistics::UpdateFrameSize(const std::string& monitor_name, int width, int height) {
        auto size = frames_size_.TryGet(monitor_name).value_or(SdkStatFrameSize{});
        size.width_ = width;
        size.height_ = height;
        frames_size_.Replace(monitor_name, size);
    }

    std::map<std::string, SdkStatFrameSize> SdkStatistics::GetFramesSize() {
        std::map<std::string, SdkStatFrameSize> frame_sizes;
        frames_size_.VisitAll(
            [&](const auto& monitor_name, const auto& frame_size) {
                frame_sizes.insert({monitor_name, frame_size});
            });
        return frame_sizes;
    }

    void SdkStatistics::CalculateDataSpeed() {
        if (recv_data_size_ >= last_recv_data_size_) {
            auto received_megabytes =
                (recv_data_size_ - last_recv_data_size_) * 1.0;
            received_megabytes /= (1024 * 1024);
            last_recv_data_size_ = recv_data_size_.load();
            recv_data_speeds_.PushBack(NumFormatter::Round2DecimalPlaces(
                static_cast<float>(received_megabytes)));
            if (recv_data_speeds_.Size() > kMaxStatCounts) {
                recv_data_speeds_.RemoveFirst();
            }
        }
        if (send_data_size_ >= last_send_data_size_) {
            auto sent_megabytes =
                (send_data_size_ - last_send_data_size_) * 1.0;
            sent_megabytes /= (1024 * 1024);
            last_send_data_size_ = send_data_size_.load();
            send_data_speeds_.PushBack(NumFormatter::Round2DecimalPlaces(
                static_cast<float>(sent_megabytes)));
            if (send_data_speeds_.Size() > kMaxStatCounts) {
                send_data_speeds_.RemoveFirst();
            }
        }
    }

    void SdkStatistics::CalculateVideoFrameFps() {
        std::map<std::string, int> frames_per_second_by_monitor;
        fps_video_recv_.VisitAll(
            [&](const auto& monitor_name, const auto& fps_statistics) {
                const auto frames_per_second = fps_statistics->value();
                frames_per_second_by_monitor.insert(
                    {monitor_name, frames_per_second});
            });

        for (const auto& [monitor_name, frames_per_second] :
             frames_per_second_by_monitor) {
            auto fps_history = video_recv_fps_.TryGet(monitor_name)
                                   .value_or(std::vector<float>{});
            fps_history.push_back(static_cast<float>(frames_per_second));
            if (fps_history.size() > kMaxStatCounts) {
                fps_history.erase(fps_history.begin());
            }
            video_recv_fps_.Replace(monitor_name, fps_history);
        }
    }

    std::map<std::string, std::vector<float>> SdkStatistics::GetVideoRecvFps() {
        std::map<std::string, std::vector<float>> video_receive_fps;
        video_recv_fps_.VisitAll(
            [&](const auto& monitor_name, const auto& fps_history) {
                video_receive_fps.insert({monitor_name, fps_history});
            });
        return video_receive_fps;
    }

    std::vector<float> SdkStatistics::GetRecvDataSpeeds() {
        std::vector<float> speeds;
        recv_data_speeds_.Visit([&](auto speed) {
            speeds.push_back(speed);
        });
        return speeds;
    }

    std::vector<float> SdkStatistics::GetSendDataSpeeds() {
        std::vector<float> speeds;
        send_data_speeds_.Visit([&](auto speed) {
            speeds.push_back(speed);
        });
        return speeds;
    }

    std::vector<float> SdkStatistics::GetNetDelays() {
        std::vector<float> delays;
        net_delays_.Visit([&](auto delay) {
            delays.push_back(delay);
        });
        return delays;
    }

    std::map<std::string, IsolatedMonitorStatisticsInfoInRender> SdkStatistics::GetRenderMonitorsStat() {
        std::map<std::string, IsolatedMonitorStatisticsInfoInRender>
            monitor_statistics;
        render_monitor_stat_.VisitAll(
            [&](const auto& monitor_name, const auto& statistics) {
                monitor_statistics.insert({monitor_name, statistics});
            });
        return monitor_statistics;
    }

    void SdkStatistics::UpdateIsolatedMonitorStatisticsInfoInRender(
        const std::string& monitor_name,
        const IsolatedMonitorStatisticsInfoInRender& statistics) {
        render_monitor_stat_.Replace(monitor_name, statistics);
    }
}
