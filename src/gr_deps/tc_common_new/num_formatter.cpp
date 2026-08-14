//
// Created by RGAA on 2024-04-23.
//

#include "num_formatter.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace tc
{
    std::string NumFormatter::FormatStorageSize(int64_t bytes) {
        const size_t GB = 1024 * 1024 * 1024;
        const size_t MB = 1024 * 1024;
        const size_t KB = 1024;

        std::stringstream stream;

        if (bytes >= GB) {
            stream << std::fixed << std::setprecision(2) << (bytes / static_cast<double>(GB)) << "GB";
        } else if (bytes >= MB) {
            stream << std::fixed << std::setprecision(2) << (bytes / static_cast<double>(MB)) << "MB";
        } else if (bytes >= KB) {
            stream << std::fixed << std::setprecision(2) << (bytes / static_cast<double>(KB)) << "KB";
        } else {
            stream << bytes << "B";
        }

        return stream.str();
    }

    std::string NumFormatter::FormatSpeed(int64_t bytes) {
        const size_t GB = 1000 * 1000 * 1000;
        const size_t MB = 1000 * 1000;
        const size_t KB = 1000;

        std::stringstream stream;

        if (bytes >= GB) {
            stream << std::fixed << std::setprecision(1) << (bytes / static_cast<double>(GB)) << "GB/S";
        } else if (bytes >= MB) {
            stream << std::fixed << std::setprecision(1) << (bytes / static_cast<double>(MB)) << "MB/S";
        } else if (bytes >= KB) {
            stream << std::fixed << std::setprecision(1) << (bytes / static_cast<double>(KB)) << "KB/S";
        } else {
            stream << bytes << "B/S";
        }

        return stream.str();
    }

    std::string NumFormatter::FormatTime(uint64_t timestamp) {
        auto total_seconds = timestamp/1000;
        int hours = total_seconds / 3600;
        int minutes = (total_seconds % 3600) / 60;
        int seconds = total_seconds % 60;

        std::stringstream stream;
        stream << std::setw(2) << std::setfill('0') << hours << ":"
               << std::setw(2) << std::setfill('0') << minutes << ":"
               << std::setw(2) << std::setfill('0') << seconds;
        return stream.str();
    }

    float NumFormatter::Round2DecimalPlaces(float num) {
        return std::round(num * 100) / 100;
    }

}
