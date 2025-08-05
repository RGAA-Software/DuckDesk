//
// Created by RGAA on 2024-02-25.
//

#include "capture_message_maker.h"

#include "tc_common_new/data.h"
#include "capture_message.h"

namespace tc
{

    std::shared_ptr<Data> CaptureMessageMaker::MakeAudioFrame(std::shared_ptr<Data>&& audio_data) {
        CaptureAudioFrame capture_audio_frame_msg{};
        capture_audio_frame_msg.frame_index_ = 0;
        auto data = Data::Make(nullptr, sizeof(CaptureAudioFrame) + audio_data->Size());
        memcpy(data->DataAddr(), &capture_audio_frame_msg, sizeof(CaptureAudioFrame));
        memcpy(data->DataAddr() + sizeof(CaptureAudioFrame), audio_data->CStr(), audio_data->Size());
        return data;
    }

    std::shared_ptr<Data> CaptureMessageMaker::MakeCaptureHelloMessage(const CaptureHelloMessage& msg) {
        auto data = Data::Make(nullptr, sizeof(CaptureHelloMessage));
        memcpy(data->DataAddr(), &msg, sizeof(CaptureHelloMessage));
        return data;
    }

    MouseEventMessage CaptureMessageMaker::MakeMouseEventMessage(uint64_t hwnd, uint32_t x, uint32_t y,
                                                                     int32_t btn, int32_t data, int32_t dx, int32_t dy,
                                                                     bool pressed, bool released) {
        MouseEventMessage msg{};
        msg.hwnd_ = hwnd;
        msg.x_ = x;
        msg.y_ = y;
        msg.button_ = btn;
        msg.data_ = data;
        msg.delta_x_ = dx;
        msg.delta_y_ = dy;
        msg.pressed_ = pressed;
        msg.released_ = released;
        return msg;
    }

    std::shared_ptr<Data> CaptureMessageMaker::MakeMouseEventMessageAsData(uint64_t hwnd, uint32_t x, uint32_t y,
                                                             int32_t btn, int32_t data, int32_t dx, int32_t dy,
                                                             bool pressed, bool released) {
        auto msg = MakeMouseEventMessage(hwnd, x, y, btn, data, dx, dy, pressed, released);
        auto msg_data = Data::Make(nullptr, sizeof(MouseEventMessage));
        memcpy(msg_data->DataAddr(), &msg, sizeof(MouseEventMessage));
        return msg_data;
    }

    KeyboardEventMessage CaptureMessageMaker::MakeKeyboardEventMessage(uint64_t hwnd_, uint32_t key, uint32_t down,
                                                          uint32_t num_lock_state, uint32_t caps_lock_state) {
        KeyboardEventMessage msg{};
        msg.hwnd_ = hwnd_;
        msg.key_ = key;
        msg.down_ = down;
        msg.num_lock_state_ = num_lock_state;
        msg.caps_lock_state_ = caps_lock_state;
        return msg;
    }

    std::shared_ptr<Data> CaptureMessageMaker::MakeKeyboardEventMessageAsData(uint64_t hwnd_, uint32_t key_, uint32_t down_,
                                                                uint32_t num_lock_state, uint32_t caps_lock_state) {
        auto msg = MakeKeyboardEventMessage(hwnd_, key_, down_, num_lock_state, caps_lock_state);
        auto msg_data = Data::Make(nullptr, sizeof(KeyboardEventMessage));
        memcpy(msg_data->DataAddr(), &msg, sizeof(KeyboardEventMessage));
        return msg_data;
    }

}
