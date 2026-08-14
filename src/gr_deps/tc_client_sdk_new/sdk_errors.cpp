//
// Created by RGAA on 20/02/2025.
//

#include "sdk_errors.h"

namespace tc
{

    std::string SdkErrorCodeToString(SdkErrorCode code) {
        if (code == SdkErrorCode::kSdkErrorOk) {
            return "ok";
        }
        else {
            return "unknown";
        }
    }

}