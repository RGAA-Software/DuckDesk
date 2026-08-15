//
// Created by RGAA on 2023-12-24.
//

#ifndef TC_APPLICATION_CAPTURE_MESSAGE_H
#define TC_APPLICATION_CAPTURE_MESSAGE_H

#include <cstdint>
#include <memory>

#include "monitor_util.h"

namespace px
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
    // app -> dll：客户端新连接时重置输入状态（清队列/差分基准/修饰键）
    constexpr auto kCaptureResetInputMessage = 0x0007;

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
        bool request_idr_ = false;
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

    // Wire format for dll -> host /ipc audio (POD only; PCM bytes follow this header).
    // Do NOT memcpy CaptureAudioFrame over the wire — it contains shared_ptrs.
#pragma pack(push, 1)
    struct IpcCaptureAudioFrame {
        uint32_t type_ = kCaptureAudioFrame;
        uint32_t data_length = 0;  // PCM byte count after this header
        uint64_t frame_index_ = 0;
        uint32_t samples_ = 48000;
        uint32_t channels_ = 2;
        uint32_t bits_ = 16;
    };
#pragma pack(pop)
    static_assert(sizeof(IpcCaptureAudioFrame) == 28, "IpcCaptureAudioFrame size");

    // Wire format for dll -> host /ipc video (POD only).
    // Do NOT memcpy CaptureVideoFrame over the wire — it contains a shared_ptr.
    // magic_/version_ guard against mixed old/new dll-host deployments.
    constexpr uint32_t kIpcCaptureVideoFrameMagic = 0x47524356;   // 'GRCV'
    constexpr uint32_t kIpcCaptureVideoFrameVersion = 1;
#pragma pack(push, 1)
    struct IpcCaptureVideoFrame {
        uint32_t magic_ = kIpcCaptureVideoFrameMagic;
        uint32_t version_ = kIpcCaptureVideoFrameVersion;
        uint32_t type_ = kCaptureVideoFrame;
        uint32_t data_length = 0;
        uint32_t capture_type_ = 0;
        uint32_t frame_width_ = 0;
        uint32_t frame_height_ = 0;
        uint32_t reserved_ = 0;
        uint64_t frame_index_ = 0;
        uint64_t frame_format_ = 0;
        uint64_t handle_ = 0;
        int64_t adapter_uid_ = -1;
        int32_t monitor_index_ = -1;
        int32_t left_ = 0;
        int32_t top_ = 0;
        int32_t right_ = 0;
        int32_t bottom_ = 0;
        int32_t request_idr_ = 0;
        char display_name_[64] = {0};
    };
#pragma pack(pop)
    static_assert(sizeof(IpcCaptureVideoFrame) == 152, "IpcCaptureVideoFrame size");

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

    // 客户端新连接：让 DLL 丢弃积压事件并重置差分基准/修饰键状态
    class CaptureResetInputMessage : public CaptureBaseMessage {
    public:
        CaptureResetInputMessage() : CaptureBaseMessage() {
            type_ = kCaptureResetInputMessage;
        }
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
        // 1 = in-process WASAPI audio hook (used when OS lacks process-loopback).
        // Written into hook_boot before inject; read by px_graphics.dll.
        uint32_t enable_hook_audio_{0};
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
