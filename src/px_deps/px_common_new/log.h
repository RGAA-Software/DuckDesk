#ifndef __SK_LOG__
#define __SK_LOG__

#include <cstdio>
#include <cstdarg>
#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>

#if defined(WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(ANDROID))
#define SPDLOG_NAME         "spd.log"
#ifndef SPDLOG_ACTIVE_LEVEL
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE
#endif
#include <spdlog/spdlog.h>
#include <spdlog/sinks/rotating_file_sink.h>
#endif

#if ANDROID
#include <android/log.h>
#define TAG "GoDesk"
#define ALOGD(...) __android_log_print(ANDROID_LOG_DEBUG,TAG ,__VA_ARGS__)
#define ALOGI(...) __android_log_print(ANDROID_LOG_INFO,TAG ,__VA_ARGS__)
#define ALOGW(...) __android_log_print(ANDROID_LOG_WARN,TAG ,__VA_ARGS__)
#define ALOGE(...) __android_log_print(ANDROID_LOG_ERROR,TAG ,__VA_ARGS__)
#define ALOGF(...) __android_log_print(ANDROID_LOG_FATAL,TAG ,__VA_ARGS__)
#endif

#include <iostream>

#define LOG_FILE 1

namespace tc
{

    #define LOGD_ SPDLOG_LOGGER_DEBUG
    #define LOGI_ SPDLOG_LOGGER_INFO
    #define LOGW_ SPDLOG_LOGGER_WARN
    #define LOGE_ SPDLOG_LOGGER_ERROR

    class Logger {
    public:

        static bool InitLog(const std::wstring& path, bool save_to_file = false) {
#if defined(WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(ANDROID))
            if (save_to_file) {
                try {
                    std::shared_ptr<spdlog::logger> logger;
                    logger = spdlog::rotating_logger_mt(SPDLOG_NAME, path, 1024 * 1024 * 50, 5, false);
                    spdlog::set_default_logger(logger);
                    logger->set_level(spdlog::level::debug);
                    logger->flush_on(spdlog::level::debug);
                    //logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%s:%#,%!][%l] : %v");
                    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%s:%#][%l] : %v");
                }
                catch (const spdlog::spdlog_ex& ex) {
                    std::cout << "Log initialization failed: " << ex.what() << std::endl;
                    return false;
                }
            }
            else {
                //spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%s:%#,%!][%l] : %v");
                spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e][thread %t][%s:%#][%l] : %v");
            }
#endif
            return true;
        }
    };

#if defined(ANDROID)
    #define LOGI(...) \
        do { \
            auto _msg_ = fmt::format(__VA_ARGS__); \
            ALOGI("%s", _msg_.c_str()); \
        } while (0)

    #define LOGW(...) \
        do { \
            auto _msg_ = fmt::format(__VA_ARGS__); \
            ALOGW("%s", _msg_.c_str()); \
        } while (0)

    #define LOGE(...) \
        do { \
            auto _msg_ = fmt::format(__VA_ARGS__); \
            ALOGE("%s", _msg_.c_str()); \
        } while (0)
#endif

#if defined(WIN32) || defined(__APPLE__) || (defined(__linux__) && !defined(ANDROID))
    #define LOGI(...) LOGI_(spdlog::default_logger_raw(), __VA_ARGS__)
    #define LOGD(...) LOGD_(spdlog::default_logger_raw(), __VA_ARGS__)
    #define LOGW(...) LOGW_(spdlog::default_logger_raw(), __VA_ARGS__)
    #define LOGE(...) LOGE_(spdlog::default_logger_raw(), __VA_ARGS__)
#endif
}


#endif
