//
// Created by RGAA on 20/02/2025.
//

#ifndef PX_SDK_ERRORS_H
#define PX_SDK_ERRORS_H

#include <string>

namespace px
{
    enum class SdkErrorCode {
        kSdkErrorOk = 0,
        kSdkErrorUnknown = 1,
    };

    static std::string SdkErrorCodeToString(SdkErrorCode code);
}

#endif //PX_SDK_ERRORS_H
