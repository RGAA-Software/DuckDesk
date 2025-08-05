//
// Created by RGAA on 2024-02-25.
//

#ifndef TC_APPLICATION_CAPTURE_MESSAGE_MAKER_H
#define TC_APPLICATION_CAPTURE_MESSAGE_MAKER_H

#include <memory>
#include "capture_message.h"
#include "tc_common_new/data.h"

namespace tc
{

    // 生成通过IPC传递的消息
    class CaptureMessageMaker {
    public:

        template<typename T>
        static std::shared_ptr<Data> ConvertMessageToData(const T& frame) {
            auto msg_data = Data::Make(nullptr, sizeof(T));
            memcpy(msg_data->DataAddr(), &frame, sizeof(T));
            return msg_data;
        }

        template<typename T>
        static std::string ConvertMessageToString(const T& msg) {
            std::string ipc_msg;
            ipc_msg.resize(sizeof(T));
            memcpy(ipc_msg.data(), &msg, sizeof(T));
            return ipc_msg;
        }

        static std::shared_ptr<Data> MakeAudioFrame(std::shared_ptr<Data>&& data);
        static std::shared_ptr<Data> MakeCaptureHelloMessage(const CaptureHelloMessage& msg);
        static MouseEventMessage MakeMouseEventMessage(uint64_t hwnd, uint32_t x, uint32_t y,
                                                                 int32_t btn, int32_t data, int32_t dx, int32_t dy,
                                                                 bool pressed, bool released);
        static std::shared_ptr<Data> MakeMouseEventMessageAsData(uint64_t hwnd, uint32_t x, uint32_t y,
                                                           int32_t btn, int32_t data, int32_t dx, int32_t dy,
                                                           bool pressed, bool released);
        static KeyboardEventMessage MakeKeyboardEventMessage(uint64_t hwnd_, uint32_t key_, uint32_t down_,
                                                            uint32_t num_lock_state, uint32_t caps_lock_state);
        static std::shared_ptr<Data> MakeKeyboardEventMessageAsData(uint64_t hwnd_, uint32_t key_, uint32_t down_,
                                                              uint32_t num_lock_state, uint32_t caps_lock_state);

    };

}
#endif //TC_APPLICATION_CAPTURE_MESSAGE_MAKER_H
