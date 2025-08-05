//
// Created by RGAA on 2023-12-24.
//

#ifndef TC_APPLICATION_CAPTURE_MESSAGE_H
#define TC_APPLICATION_CAPTURE_MESSAGE_H

#include <cstdint>
#include <memory>

#include "win/desktop_capture/monitor_util.h"

namespace tc
{
    // type_
    // dll -> app
    class Data;
    class Image;

    constexpr auto kCaptureVideoFrame = 0x0001;
    // dll -> app
    constexpr auto kCaptureAudioFrame = 0x0002;
    // dll -> app
    constexpr auto kCaptureDebugInfo = 0x0003;
    // app -> dll
    constexpr auto kCaptureHelloMessage = 0x0004;
    // app -> dll
    constexpr auto kMouseEventMessage = 0x0005;
    // app -> dll
    constexpr auto kKeyboardEventMessage = 0x0006;

    // capture_type_
    constexpr auto kCaptureVideoByHandle = 0x1000;
    constexpr auto kCaptureVideoBySharedMemory = 0x1001;
    constexpr auto kCaptureVideoByBitmapData = 0x1002;

    class CaptureBaseMessage {
    public:
        uint32_t type_ = 0;
        // shm 中的数据大小
        uint32_t data_length = 0;
    };

    class CaptureVideoFrame : public CaptureBaseMessage {
    public:
        CaptureVideoFrame() : CaptureBaseMessage() {
            type_ = kCaptureVideoFrame;
        }
    public:
        // constexpr auto kCaptureVideoByHandle = 0x1000;
        // constexpr auto kCaptureVideoBySharedMemory = 0x1001;
        uint32_t capture_type_ = 0;
        uint32_t frame_width_ = 0;
        uint32_t frame_height_ = 0;
        uint64_t frame_index_ = 0;
        uint64_t frame_format_ = 0;
        uint64_t handle_ = 0;
        int64_t adapter_uid_ = -1;
        char display_name_[64] = {0};
        int monitor_index_ = -1;
        int left_{};
        int top_{};
        int right_{};
        int bottom_{};
        std::shared_ptr<Image> raw_image_ = nullptr;
    };

    class CaptureAudioFrame: public CaptureBaseMessage {
    public:
        CaptureAudioFrame() : CaptureBaseMessage() {
            type_ = kCaptureAudioFrame;
        }
    public:
        uint64_t frame_index_{};
        uint32_t samples_ = 0;
        uint32_t channels_ = 0;
        uint32_t bits_ = 0;
        std::shared_ptr<Data> full_data_ = nullptr;
        std::shared_ptr<Data> left_ch_data_ = nullptr;
        std::shared_ptr<Data> right_ch_data_ = nullptr;
    };

    class CaptureDebugInfo : public CaptureBaseMessage {
    public:

    };

    // Send this message from app to dll when the dll is injected.
    class CaptureHelloMessage : public CaptureBaseMessage {
    public:
        CaptureHelloMessage() : CaptureBaseMessage() {
            type_ = kCaptureHelloMessage;
        }
    public:
#ifdef WIN32
        // [d3d8]
        // present=0x0
        // [d3d9]
        // present=0xb73f0
        // present_ex=0xb7490
        // present_swap=0xc470
        // d3d9_clsoff=0x4030
        // is_d3d9ex_clsoff=0x55a0
        // [dxgi]
        // present=0x15e0
        // present1=0x68dc0
        // resize=0x22f40
        // release=0x3240
        uint64_t d3d9_present = 0;
        uint64_t d3d9_present_ex = 0;
        uint64_t d3d9_present_swap = 0;
        uint64_t d3d9_d3d9_clsoff = 0;
        uint64_t d3d9_is_d3d9ex_clsoff = 0;

        uint64_t dxgi_present = 0;
        uint64_t dxgi_present1 = 0;
        uint64_t dxgi_resize = 0;
        uint64_t dxgi_release = 0;

#endif
    };

    class MouseEventMessage : public CaptureBaseMessage {
    public:
        MouseEventMessage() : CaptureBaseMessage() {
            type_ = kMouseEventMessage;
        }
    public:
        uint64_t hwnd_{};
        // x , from top-left
        uint32_t x_ = 0;
        // y, from top-left
        uint32_t y_ = 0;
        // ref: https://docs.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mouse_event
        int32_t button_ = 0;
        int32_t pressed_ = 0;
        int32_t released_ = 0;
        // wheel data
        int32_t data_ = 0;
        int32_t delta_x_ = 0;
        int32_t delta_y_ = 0;
        int32_t absolute_ = 0;
    };

    class KeyboardEventMessage : public CaptureBaseMessage {
    public:
        KeyboardEventMessage() : CaptureBaseMessage() {
            type_ = kKeyboardEventMessage;
        }
    public:
        uint64_t hwnd_{};
        uint32_t key_{};
        uint32_t down_{};
        uint32_t num_lock_state_{};
        uint32_t caps_lock_state_{};
    };

    // 桌面模式下采集鼠标的信息
    class CaptureCursorBitmap : public CaptureBaseMessage {
    public:
        uint32_t width_ = 0;
        uint32_t height_ = 0;
        int32_t hotspot_x_ = 0;
        int32_t hotspot_y_ = 0;
        int32_t x_ = 0;
        int32_t y_ = 0;
        bool visible_ = true;
        std::shared_ptr<Data> data_ = nullptr;
        uint32_t type_;
    };

    //
    class AppSharedMessage : public CaptureHelloMessage {
    public:
        //
        uint32_t ipc_port_{0};
        uint32_t self_size_{0};
        uint32_t enable_hook_events_{0};
    };

    // current capturing monitor info
    // from capture plugin
    class CaptureMonitorInfoMessage : public CaptureBaseMessage {
    public:
        std::vector<CaptureMonitorInfo> monitors_;
        std::string capturing_monitor_name_;
        VirtualDesktopBoundRectangleInfo virtual_desktop_bound_rectangle_info_;
    };
    
    // 弃用
    class RefreshScreenMessage {
    public:
    };

    class CaptureInitFailedMessage {
    public:
    };

}

#endif //TC_APPLICATION_CAPTURE_MESSAGE_H
