#ifndef PX_COMMON_NEW_RTC_MONITOR_TRACK_SLOTS_H
#define PX_COMMON_NEW_RTC_MONITOR_TRACK_SLOTS_H

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace px
{

    // Capture frame indices are scoped to a monitor's current capture stream.
    // A display topology change can restart that stream from a lower value, so
    // consumers must never derive a frame gap with an unchecked unsigned
    // subtraction.
    enum class RtcFrameSequenceDisposition {
        kFirstFrame,
        kConsecutive,
        kForwardGap,
        kReset,
    };

    struct RtcFrameSequenceState {
        bool initialized_ = false;
        uint64_t last_frame_index_ = 0;
    };

    struct RtcFrameSequenceResult {
        RtcFrameSequenceDisposition disposition_ = RtcFrameSequenceDisposition::kFirstFrame;
        uint64_t gap_ = 0;
    };

    inline RtcFrameSequenceResult AdvanceRtcFrameSequence(
        RtcFrameSequenceState& state,
        uint64_t frame_index) {
        if (!state.initialized_) {
            state.initialized_ = true;
            state.last_frame_index_ = frame_index;
            return {};
        }

        const uint64_t previous_frame_index = state.last_frame_index_;
        state.last_frame_index_ = frame_index;
        if (frame_index <= previous_frame_index) {
            return { RtcFrameSequenceDisposition::kReset, 0 };
        }

        const uint64_t gap = frame_index - previous_frame_index;
        return {
            gap == 1
                ? RtcFrameSequenceDisposition::kConsecutive
                : RtcFrameSequenceDisposition::kForwardGap,
            gap,
        };
    }

    inline bool ShouldResetRtcCaptureStream(
        bool topology_rebound,
        const RtcFrameSequenceResult& sequence_result) {
        return topology_rebound
            || sequence_result.disposition_ == RtcFrameSequenceDisposition::kReset;
    }

    // Keep negotiated RTC track identities stable while monitors are hot-added or
    // removed. Existing active monitor assignments are preserved; disappeared
    // monitors release their slots and new monitors take the first free slot.
    inline bool ReconcileRtcMonitorTrackSlots(
        std::vector<std::string>& track_slots,
        const std::vector<std::string>& active_monitors) {
        const std::unordered_set<std::string> active_names(
            active_monitors.begin(), active_monitors.end());
        const auto previous_slots = track_slots;

        for (auto& slot : track_slots) {
            if (!slot.empty() && !active_names.contains(slot)) {
                slot.clear();
            }
        }

        for (const auto& monitor_name : active_monitors) {
            if (monitor_name.empty()
                || std::find(track_slots.begin(), track_slots.end(), monitor_name)
                    != track_slots.end()) {
                continue;
            }
            const auto free_slot = std::find(track_slots.begin(), track_slots.end(), "");
            if (free_slot == track_slots.end()) {
                break;
            }
            *free_slot = monitor_name;
        }
        return track_slots != previous_slots;
    }

}

#endif // PX_COMMON_NEW_RTC_MONITOR_TRACK_SLOTS_H
