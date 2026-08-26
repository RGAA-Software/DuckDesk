#ifndef PX_COMMON_NEW_RTC_MONITOR_TRACK_SLOTS_H
#define PX_COMMON_NEW_RTC_MONITOR_TRACK_SLOTS_H

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

namespace px
{

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
