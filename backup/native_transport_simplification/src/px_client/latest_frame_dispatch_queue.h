#pragma once

#include <map>
#include <mutex>
#include <utility>
#include <vector>

namespace px
{

    // Keeps the UI dispatch queue bounded when decoded frames arrive faster than
    // the GUI thread can render them. The first frame schedules a GUI task; while
    // that task is pending, only the latest frame for each monitor is retained.
    template<typename Key, typename Frame>
    class LatestFrameDispatchQueue {
    public:
        bool Push(const Key& key, Frame frame) {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return false;
            }

            pending_frames_.insert_or_assign(key, std::move(frame));
            if (ui_task_scheduled_) {
                return false;
            }

            ui_task_scheduled_ = true;
            return true;
        }

        std::vector<Frame> TakePending() {
            std::lock_guard lock(mutex_);
            std::vector<Frame> frames;
            frames.reserve(pending_frames_.size());
            for (auto& [key, frame] : pending_frames_) {
                frames.push_back(std::move(frame));
            }
            pending_frames_.clear();
            ui_task_scheduled_ = false;
            return frames;
        }

        void Stop() {
            std::lock_guard lock(mutex_);
            stopped_ = true;
            pending_frames_.clear();
            ui_task_scheduled_ = false;
        }

    private:
        std::mutex mutex_;
        std::map<Key, Frame> pending_frames_;
        bool ui_task_scheduled_ = false;
        bool stopped_ = false;
    };

}
