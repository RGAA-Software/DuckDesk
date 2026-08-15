//
// Created by RGAA on 12/06/2025.
//

#ifndef GAMMARAY_RELAY_ERRORS_H
#define GAMMARAY_RELAY_ERRORS_H

#include <string>

namespace relay
{

    constexpr int kRelayOk = 200;
    //
    constexpr int kRelayParseJsonFailed = 601;
    constexpr int kRelayRequestFailed = 602;
    // server resp
    constexpr int kErrParamInvalid = 700;
    constexpr int kErrOperateDBFailed = 701;
    constexpr int kErrRoomNotFound = 702;
    constexpr int kErrDeviceNotFound = 703;

    static std::string RelayError2String(const int err) {
        if (err == kRelayOk) {
            return "Ok";
        }
        else if (err == kRelayParseJsonFailed) {
            return "Parse json failed";
        }
        else if (err == kRelayRequestFailed) {
            return "Network request failed";
        }
        else if (err == kErrParamInvalid) {
            return "Params invalid";
        }
        else if (err == kErrOperateDBFailed) {
            return "Operate DB Failed";
        }
        else if (err == kErrRoomNotFound) {
            return "Room not found";
        }
        else if (err == kErrDeviceNotFound) {
            return "Device not found";
        }
        else {
            return "Unknown";
        }
    }
}

#endif //GAMMARAY_RELAY_ERRORS_H
