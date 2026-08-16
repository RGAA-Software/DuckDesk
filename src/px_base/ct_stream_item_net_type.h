//
// Created by RGAA on 24/03/2025.
//

#ifndef PX_CT_STREAM_ITEM_NET_TYPE_H
#define PX_CT_STREAM_ITEM_NET_TYPE_H

#include <string>

namespace px
{

    static std::string kStreamItemNtTypeWebSocket = "websocket";
    static std::string kStreamItemNtTypeUdpKcp = "udp_kcp";
    static std::string kStreamItemNtTypeRelay = "relay";
    static std::string kStreamItemNtTypeWebRTCDirect = "webrtc_direct";
    static std::string kStreamItemNtTypeUdpDirect = "udp_direct";
    static std::string kStreamItemNtTypeWebRTC = "webrtc";

    static std::string kStreamItemConnTypeDirect = "direct";
    static std::string kStreamItemConnTypeSignaling = "signaling";

}

#endif //PX_CT_STREAM_ITEM_NET_TYPE_H
