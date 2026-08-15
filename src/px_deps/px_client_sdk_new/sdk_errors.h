//
// Created by RGAA on 20/02/2025.
//

#ifndef GAMMARAY_SDK_ERRORS_H
#define GAMMARAY_SDK_ERRORS_H

#include <string>

namespace tc
{
    enum class SdkErrorCode {
        kSdkErrorOk = 0,
        kSdkErrorUnknown = 1,
    };

    static std::string SdkErrorCodeToString(SdkErrorCode code);
}

#endif //GAMMARAY_SDK_ERRORS_H
