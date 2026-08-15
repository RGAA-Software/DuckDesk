//
// Created by RGAA on 2024-04-23.
//

#ifndef GAMMARAY_NUM_FORMATTER_H
#define GAMMARAY_NUM_FORMATTER_H

#include <string>

namespace tc
{

    class NumFormatter {
    public:

        static std::string FormatStorageSize(int64_t bytes);
        static std::string FormatSpeed(int64_t bytes);
        static std::string FormatTime(uint64_t timestamp);
        static float Round2DecimalPlaces(float num);

    };

}

#endif //GAMMARAY_NUM_FORMATTER_H
