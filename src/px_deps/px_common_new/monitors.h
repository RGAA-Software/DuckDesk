//
// Created by RGAA  on 2024/5/16.
//

#ifndef GAMMARAY_MONITORS_H
#define GAMMARAY_MONITORS_H

#ifdef WIN32
#include <dxgi.h>
#include <vector>
#include <cstdint>
#include <string>

#include "string_util.h"

namespace tc
{

    class MonitorWinInfo {
    public:
        using MonitorIndex = uint32_t;

        MonitorWinInfo() {
            monitor_handle = nullptr;
        }

        explicit MonitorWinInfo(HMONITOR monitor_handle) {
            monitor_handle = monitor_handle;
            MONITORINFOEX monitor_info = {sizeof(monitor_info)};
            GetMonitorInfo(monitor_handle, &monitor_info);
            std::wstring display_name(monitor_info.szDevice);
            name_ = StringUtil::ToUTF8(display_name);
            is_primary_ = (monitor_info.dwFlags & MONITORINFOF_PRIMARY) != 0;
        }

        std::string name_;
        bool is_primary_{};
        HMONITOR monitor_handle{};
    };

    static std::vector<MonitorWinInfo> EnumerateAllMonitors() {
        std::vector<MonitorWinInfo> monitors;
        EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hmon, HDC, LPRECT, LPARAM lparam) {
            auto &monitors = *reinterpret_cast<std::vector<MonitorWinInfo> *>(lparam);
            monitors.emplace_back(hmon);
            return TRUE;
        }, reinterpret_cast<LPARAM>(&monitors));

        return monitors;
    }

}


#endif //GAMMARAY_MONITORS_H
#endif