//
// Created by RGAA on 2024/1/25.
//

#ifndef TC_CLIENT_ANDROID_SDK_MESSAGES_H
#define TC_CLIENT_ANDROID_SDK_MESSAGES_H

#include <string>
#include <memory>

#include "sdk_errors.h"
#include "connection/udp_media_state.h"
#include "px_message.pb.h"
#include "px_common/ws_control_signal.h"

namespace px {

class RawImage;

// monitor info
// mon_right_ - mon_left_ != frame_width_ ; cuz awareness settings in Renderer
class SdkCaptureMonitorInfo {
  public:
    std::string mon_name_;
    int mon_index_ = -1;
    int mon_left_ = 0;
    int mon_top_ = 0;
    int mon_right_ = 0;
    int mon_bottom_ = 0;
    int frame_width_ = -1;
    int frame_height_ = -1;
    uint64_t update_time_ = 0;
};

class SdkMsgTimer1000 {};

class SdkMsgTimer2000 {};

class SdkMsgTimer100 {};

class SdkMsgTimer16 {};

// change monitor resolution
class SdkMsgChangeMonitorResolutionResult {
  public:
    std::string monitor_name_;
    bool result = false;
};

// errors
class SdkMsgError {
  public:
    SdkErrorCode code_{SdkErrorCode::kSdkErrorUnknown};
    std::string msg_;
};

struct SdkMsgUdpMediaUnavailable {
    UdpMediaFailure reason{UdpMediaFailure::kProbeTimeout};
};

class SdkMsgWsConnectionRejected {
  public:
    WsControlRejection rejection_ = WsControlRejection::kSessionPolicy;
};

class SdkMsgNetworkConnected {
  public:
};

class SdkMsgNetworkDisConnected {
  public:
};

// render 主动断开本连接(kConnectionTakenOver):被其它客户端接管,
// 需要明确提示并停止重连,而不是走普通断线重连流程
class SdkMsgConnectionTakenOver {
  public:
};

class SdkMsgFirstConfigInfoCallback {
  public:
    std::shared_ptr<px::Message> msg_ = nullptr;
};

class SdkMsgFirstVideoFrameDecoded {
  public:
    std::shared_ptr<RawImage> raw_image_ = nullptr;
    SdkCaptureMonitorInfo mon_info_;
};

class SdkMsgVideoDecodeInit {
  public:
    EImageFormat format_ = EImageFormat::kI420;
    int width_ = 0;
    int height_ = 0;
    bool hard_ware_ = false;
};
} // namespace px

#endif // TC_CLIENT_ANDROID_SDK_MESSAGES_H
