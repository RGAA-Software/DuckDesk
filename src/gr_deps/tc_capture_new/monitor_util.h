//
// Created by RGAA  on 2024/1/3.
//

#ifndef WIN_CAPTURE_MONITOR_UTIL_H
#define WIN_CAPTURE_MONITOR_UTIL_H

#include <dxgi.h>
#include <string>
#include <string>
#include <sstream>
#include <vector>

namespace tc
{
    using MonitorIndex = uint32_t;

    class SupportedResolution {
    public:
        unsigned long width_ = 0;
        unsigned long height_ = 0;
    };

    class VirtualDesktopBoundRectangleInfo {
    public:
        int far_left_ = 0;
        int far_top_ = 0;
        int far_right_ = 0;
        int far_bottom_ = 0;
        [[nodiscard]] std::string Dump() const {
            std::stringstream ss;
            ss << "VirtualDesktopBoundRectangleInfo far_left: " << far_left_ << ", far_top: " << far_top_ << ", far_right: " << far_right_ << ", far_bottom: " << far_bottom_ << std::endl;
            return ss.str();
        }
    };

    class CaptureMonitorInfo {
    public:
        std::string name_;
        bool primary_{};//to do:尚未生效
        bool attached_desktop_{};
        long top_{};
        long left_{};
        long right_{};
        long bottom_{};

        // 整个虚拟桌面信息
        //long virtual_desktop_top_{};
        //long virtual_desktop_left_{};
        //long virtual_desktop_right_{};
        //long virtual_desktop_bottom_{};
        //long virtual_desktop_width_;
        //long virtual_desktop_height_;
        
        std::vector<SupportedResolution> supported_res_;
        unsigned long adapter_uid_ = 0;
    public:

        [[nodiscard]] long Width() const {
            return right_ - left_;
        }

        [[nodiscard]] long Height() const {
            return bottom_ - top_;
        }

        [[nodiscard]] bool Valid() const {
            if (name_.empty()) {
                return false;
            }
            // to do: kVirtualDesktopNameSign 定义在 gr_monitor_capture_plugin.h , 等将monitor_util.h 移出去
//            if ("Virtual_Desktop" == name_) {
//                return virtual_desktop_width_ > 0 && virtual_desktop_height_ > 0;
//            }

            return right_ > left_ && bottom_ > top_;
        }

//        [[nodiscard]] long VirtualDesktopWidth() const {
//            return virtual_desktop_width_;
//        }
//
//        [[nodiscard]] long VirtualDesktopHeight() const {
//            return virtual_desktop_height_;
//        }

        [[nodiscard]] std::string Dump() const {
            std::stringstream ss;
            ss << "name: " << name_ << std::endl;
            ss << "attached desktop: " << attached_desktop_ << std::endl;
            ss << "left: " << left_ << ", top: " << top_ << ", right: " << right_ << ", bottom: " << bottom_ << std::endl;
//            ss << "virtual_desktop_top_: " << virtual_desktop_top_ << ", virtual_desktop_left_: " << virtual_desktop_left_ <<
//                ", virtual_desktop_width_: " << virtual_desktop_width_ << ", virtual_desktop_height_: " << virtual_desktop_height_ << std::endl;
            return ss.str();
        }
    };

}
#endif //WIN_CAPTURE_MONITOR_UTIL_H
