//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_TIMEEXT_H
#define TC_APPLICATION_TIMEEXT_H

#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <string>
#include <thread>

namespace px
{

    class TimeUtil {
    public:

        [[nodiscard]] static uint64_t GetCurrentTimestamp() {
            const auto now = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            return static_cast<uint64_t>(now.time_since_epoch().count());
        }

        [[nodiscard]] static std::string FormatTimestamp(uint64_t time, bool with_ms = false) {
            return FormatLocalTime(time, "%Y-%m-%d %H:%M:%S", with_ms);
        }

        [[nodiscard]] static std::string FormatTimestamp2(uint64_t time, bool with_ms = false) {
            return FormatLocalTime(time, "%Y_%m_%d-%H_%M_%S", with_ms);
        }

        [[nodiscard]] static std::string GetCurrentTimeString() {
            return FormatTimestamp2(GetCurrentTimestamp());
        }

        [[nodiscard]] static uint64_t GetCurrentTimePointUS() {
            const auto now = std::chrono::steady_clock::now();
            return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count());
        }

        static void DelayBySleep(int ms) {
            if (ms > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(ms));
            }
        }

        static void DelayByCount(int milliseconds) {
            if (milliseconds <= 0) {
                return;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
            while (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
        }

        [[nodiscard]] static std::string FormatSecondsToDHMS(long long totalSeconds) {
            // 计算各个时间单位
            const int secondsPerMinute = 60;
            const int secondsPerHour = 60 * secondsPerMinute;
            const int secondsPerDay = 24 * secondsPerHour;

            long long days = totalSeconds / secondsPerDay;
            long long remainingSeconds = totalSeconds % secondsPerDay;

            long long hours = remainingSeconds / secondsPerHour;
            remainingSeconds %= secondsPerHour;

            long long minutes = remainingSeconds / secondsPerMinute;
            long long seconds = remainingSeconds % secondsPerMinute;

            // 构建输出字符串
            std::stringstream ss;

            if (days > 0) {
                ss << days << "D ";
            }

            ss << std::setw(2) << std::setfill('0') << hours << ":"
               << std::setw(2) << std::setfill('0') << minutes << ":"
               << std::setw(2) << std::setfill('0') << seconds;

            return ss.str();
        }

    private:
        template <std::size_t Size>
        [[nodiscard]] static std::string FormatLocalTime(uint64_t timestamp_ms, const char (&format)[Size], bool with_ms) {
            const auto seconds = static_cast<std::time_t>(timestamp_ms / 1000);
            std::tm time_info{};
#ifdef _WIN32
            if (localtime_s(&time_info, &seconds) != 0) {
#else
            if (localtime_r(&seconds, &time_info) == nullptr) {
#endif
                return {};
            }

            std::array<char, 80> buffer{};
            if (std::strftime(buffer.data(), buffer.size(), format, &time_info) == 0) {
                return {};
            }
            std::string result{buffer.data()};
            if (with_ms) {
                result += "." + std::to_string(timestamp_ms % 1000);
            }
            return result;
        }
    };

    class TimeDuration {
    public:
        explicit TimeDuration(std::string name);
        ~TimeDuration();

    private:
        std::chrono::steady_clock::time_point begin_time_{};
        std::string name_;
    };

}

#endif //TC_APPLICATION_TIMEEXT_H
