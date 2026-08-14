//
// Created by RGAA on 2024/1/8.
//

#ifndef TC_COMMON_FPS_STAT_H
#define TC_COMMON_FPS_STAT_H

#include <cstdint>
#include <queue>
#include <chrono>
#include <mutex>
#include "log.h"

namespace tc
{

    class FpsStat {
    public:
        explicit FpsStat(size_t max_sample = 144) : max_samples_(max_sample) {
            last_frame_time_ = std::chrono::steady_clock::now();
        }

        void Tick() {
            std::lock_guard<std::mutex> lk(mtx_);
            auto now = std::chrono::steady_clock::now();
            std::chrono::duration<double> elapsedSeconds = now - last_frame_time_;
            last_frame_time_ = now;

            double currentFrameTime = elapsedSeconds.count();
            frame_times_.push_back(currentFrameTime);
            if (frame_times_.size() > max_samples_) {
                frame_times_.erase(frame_times_.begin());
            }
        }

        [[nodiscard]] int value() {
            std::lock_guard<std::mutex> lk(mtx_);
            if (frame_times_.empty()) return 0.0;
            double avg_frame_time = 0.0;
            for (auto time : frame_times_) {
                avg_frame_time += time;
            }
            avg_frame_time /= (double)frame_times_.size();
            auto fps = 1.0 / avg_frame_time;
            return (int)fps;
        }

    private:
        std::chrono::time_point<std::chrono::steady_clock> last_frame_time_;
        std::vector<double> frame_times_;
        const size_t max_samples_;
        std::mutex mtx_;
    };
}

#endif
