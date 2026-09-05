//
// Created by RGAA on 2024-02-25.
//

#ifndef TC_APPLICATION_CAPTURE_MESSAGE_MAKER_H
#define TC_APPLICATION_CAPTURE_MESSAGE_MAKER_H

#include <memory>
#include "capture_message.h"
#include "px_common/data.h"

namespace px
{

    // 生成通过IPC传递的消息
    class CaptureMessageMaker {
    public:

        template<typename T>
        static std::string ConvertMessageToString(const T& msg) {
            std::string ipc_msg;
            ipc_msg.resize(sizeof(T));
            memcpy(ipc_msg.data(), &msg, sizeof(T));
            return ipc_msg;
        }

        static std::string MakeIpcAudioFrameString(const void* pcm,
                                                  int pcm_bytes,
                                                  uint32_t samples,
                                                  uint32_t channels,
                                                  uint32_t bits,
                                                  uint64_t frame_index) {
            if (!pcm || pcm_bytes <= 0) {
                return {};
            }
            IpcCaptureAudioFrame hdr{};
            hdr.type_ = kCaptureAudioFrame;
            hdr.data_length = static_cast<uint32_t>(pcm_bytes);
            hdr.frame_index_ = frame_index;
            hdr.samples_ = samples;
            hdr.channels_ = channels;
            hdr.bits_ = bits;
            std::string out;
            out.resize(sizeof(hdr) + static_cast<size_t>(pcm_bytes));
            memcpy(out.data(), &hdr, sizeof(hdr));
            memcpy(out.data() + sizeof(hdr), pcm, static_cast<size_t>(pcm_bytes));
            return out;
        }

        static std::shared_ptr<Data> MakeCaptureHelloMessage(const CaptureHelloMessage& msg) {
            auto data = Data::Allocate( sizeof(CaptureHelloMessage));
            memcpy(data->MutableBytes().data(), &msg, sizeof(CaptureHelloMessage));
            return data;
        }

        static MouseEventMessage MakeMouseEventMessage(uint64_t hwnd, uint32_t x, uint32_t y,
                                                                 int32_t btn, int32_t data,
                                                                 bool pressed, bool released) {
            MouseEventMessage msg{};
            msg.hwnd_ = hwnd;
            msg.x_ = x;
            msg.y_ = y;
            msg.button_ = btn;
            msg.data_ = data;
            msg.pressed_ = pressed;
            msg.released_ = released;
            return msg;
        }
        static std::shared_ptr<Data> MakeMouseEventMessageAsData(uint64_t hwnd, uint32_t x, uint32_t y,
                                                           int32_t btn, int32_t data,
                                                           bool pressed, bool released) {
            auto msg = MakeMouseEventMessage(hwnd, x, y, btn, data, pressed, released);
            auto msg_data = Data::Allocate( sizeof(MouseEventMessage));
            memcpy(msg_data->MutableBytes().data(), &msg, sizeof(MouseEventMessage));
            return msg_data;
        }

        static KeyboardEventMessage MakeKeyboardEventMessage(uint64_t hwnd, uint32_t key, uint32_t down,
                                                            uint32_t num_lock_state, uint32_t caps_lock_state) {
            KeyboardEventMessage msg{};
            msg.hwnd_ = hwnd;
            msg.key_ = key;
            msg.down_ = down;
            msg.num_lock_state_ = num_lock_state;
            msg.caps_lock_state_ = caps_lock_state;
            return msg;
        }

        static std::shared_ptr<Data> MakeKeyboardEventMessageAsData(uint64_t hwnd_, uint32_t key_, uint32_t down_,
                                                              uint32_t num_lock_state, uint32_t caps_lock_state) {
            auto msg = MakeKeyboardEventMessage(hwnd_, key_, down_, num_lock_state, caps_lock_state);
            auto msg_data = Data::Allocate( sizeof(KeyboardEventMessage));
            memcpy(msg_data->MutableBytes().data(), &msg, sizeof(KeyboardEventMessage));
            return msg_data;
        }

    };

}
#endif //TC_APPLICATION_CAPTURE_MESSAGE_MAKER_H
