//
// Created by RGAA on 2024/1/26.
//

#ifndef TC_CLIENT_ANDROID_STREAM_HELPER_H
#define TC_CLIENT_ANDROID_STREAM_HELPER_H

#include <string>
#include <functional>

namespace tc
{

    class StreamHelper {
    public:

        static void SplitH264(const char *ptr, int len, int prefix, const std::function<void(const char *, int, int)> &cb);
        static int ConvertH264SPSPPS(const uint8_t *p_buf, size_t i_buf_size,
                              uint8_t * out_sps_buf, size_t * out_sps_buf_size,
                              uint8_t * out_pps_buf, size_t * out_pps_buf_size);
        static std::string ConvertVPSH265SPSPPS(const uint8_t *p_buf, size_t i_buf_size);

    };

}

#endif //TC_CLIENT_ANDROID_STREAM_HELPER_H
