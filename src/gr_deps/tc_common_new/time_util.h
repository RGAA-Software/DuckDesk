//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_TIMEEXT_H
#define TC_APPLICATION_TIMEEXT_H

#include <chrono>
#include <string>
#include <iomanip>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <timeapi.h>
#endif
#include <thread>

namespace tc
{

    class TimeUtil {
    public:

        static uint64_t GetCurrentTimestamp() {
            std::chrono::time_point<std::chrono::system_clock,std::chrono::milliseconds> tp
                    = std::chrono::time_point_cast<std::chrono::milliseconds>(std::chrono::system_clock::now());
            std::time_t timestamp = tp.time_since_epoch().count();
            return timestamp;
        }

        static std::string FormatTimestamp(uint64_t time, bool with_ms = false) {
            time_t seconds = time / 1000;
            std::tm timeinfo = *std::localtime(&seconds);
            char buffer[80];
            std::strftime(buffer, 80, "%Y-%m-%d %H:%M:%S", &timeinfo);
            return std::string(buffer) + (with_ms ? ("." + std::to_string(time % 1000)) : "");
        }

        static std::string FormatTimestamp2(uint64_t time, bool with_ms = false) {
            time_t seconds = time / 1000;
            std::tm timeinfo = *std::localtime(&seconds);
            char buffer[80];
            std::strftime(buffer, 80, "%Y_%m_%d-%H_%M_%S", &timeinfo);
            return std::string(buffer) + (with_ms ? ("." + std::to_string(time % 1000)) : "");
        }

        static std::string GetCurrentTimeString() {
            auto ts = GetCurrentTimestamp();
            return FormatTimestamp2(ts);
        }

        static uint64_t GetCurrentTimePointUS() {
            auto now = std::chrono::high_resolution_clock::now();
            auto microseconds = std::chrono::duration_cast<std::chrono::microseconds>(now.time_since_epoch()).count();
            return microseconds;
        }

        // 1ms => [1ms, 2ms]
        static void DelayBySleep(int ms) {
#ifdef _WIN32
            timeBeginPeriod(1);
#endif
            std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#ifdef _WIN32
            timeEndPeriod(1);
#endif
        }

        // 1ms => 1ms but more cpu usage
        static void DelayByCount(int milliseconds) {
#ifdef _WIN32
            LARGE_INTEGER frequency, start;
            QueryPerformanceFrequency(&frequency);
            QueryPerformanceCounter(&start);
            const long long target = start.QuadPart + (frequency.QuadPart * milliseconds) / 1000;

            LARGE_INTEGER current;
            do {
                QueryPerformanceCounter(&current);
            } while (current.QuadPart < target);
#else
            struct timespec req = {
                static_cast<time_t>(milliseconds / 1000),          // 秒
                static_cast<long>((milliseconds % 1000) * 1000000) // 纳秒
            };
            struct timespec rem;
            clock_nanosleep(CLOCK_MONOTONIC, 0, &req, &rem);
#endif
        }

        static std::string FormatSecondsToDHMS(long long totalSeconds) {
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


    };

    class TimeDuration {
    public:
        TimeDuration(const std::string& name);
        ~TimeDuration();

    private:
        uint64_t begin_ts_ = 0;
        std::string name_;
    };

}

#endif //TC_APPLICATION_TIMEEXT_H
