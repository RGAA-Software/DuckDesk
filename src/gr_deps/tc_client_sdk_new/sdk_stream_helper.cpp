//
// Created by RGAA on 2024/1/26.
//

#include "sdk_stream_helper.h"

#include "tc_common_new/log.h"

namespace tc
{

#define H264_TYPE(v) ((uint8_t)(v) & 0x1F)
#define H265_TYPE(v) (((uint8_t)(v) >> 1) & 0x3f)

    namespace ENalType
    {
        enum Type
        {
            H264_NAL_IDR = 5,
            H264_NAL_SPS = 7,
            H264_NAL_PPS = 8,
            H265_NAL_VPS = 32,
            H265_NAL_SPS = 33,
            H265_NAL_PPS = 34,
        };
    }

    static const char *memfind(const char *buf, int len, const char *subbuf, int sublen) {
        for (auto i = 0; i < len - sublen; ++i) {
            if (memcmp(buf + i, subbuf, sublen) == 0) {
                return buf + i;
            }
        }
        return nullptr;
    }

    void StreamHelper::SplitH264(const char *ptr, int len, int prefix, const std::function<void(const char *, int, int)> &cb) {
        auto start = ptr + prefix;
        auto end = ptr + len;
        int next_prefix;
        while (true) {
            auto next_start = memfind(start, end - start, "\x00\x00\x01", 3);
            if (next_start) {
                //找到下一帧
                if (*(next_start - 1) == 0x00) {
                    //这个是00 00 00 01开头
                    next_start -= 1;
                    next_prefix = 4;
                }
                else {
                    //这个是00 00 01开头
                    next_prefix = 3;
                }
                //记得加上本帧prefix长度
                cb(start - prefix, next_start - start + prefix, prefix);
                //搜索下一帧末尾的起始位置
                start = next_start + next_prefix;
                //记录下一帧的prefix长度
                prefix = next_prefix;
                continue;
            }
            //未找到下一帧,这是最后一帧
            cb(start - prefix, end - start + prefix, prefix);
            break;
        }
    }

    int StreamHelper::ConvertH264SPSPPS(const uint8_t *p_buf, size_t i_buf_size,
                          uint8_t * out_sps_buf, size_t * out_sps_buf_size,
                          uint8_t * out_pps_buf, size_t * out_pps_buf_size) {
        SplitH264((const  char*)p_buf, i_buf_size, 0, [&](const char* buf, int a1, int a2) {
            int off = 0;
            if (H264_TYPE(buf[4]) == 7)//SPS
            {

                memcpy(out_sps_buf, buf + off, a1 - off);
                *out_sps_buf_size = a1 - off;
                LOGI("sps size:{}", a1 - off);
            }
            if (H264_TYPE(buf[4]) == 8)//PPS
            {
                memcpy(out_pps_buf, buf + off, a1 - off);
                *out_pps_buf_size = a1 - off;
                LOGI("pps size:{}", a1 - off);
            }
            //LOGE("split ok type:%d a1: %d a2:%d",H264_TYPE(buf[4]),a1,a2);
        });
        return 0;
    }

    std::string StreamHelper::ConvertVPSH265SPSPPS(const uint8_t *p_buf, size_t i_buf_size) {
        std::string ret;
        std::string vps,sps,pps;
        SplitH264((const  char*)p_buf, i_buf_size, 4, [&](const char* buf, int a1, int a2) {
            switch (H265_TYPE(buf[4])) {
                case ENalType::H265_NAL_VPS:
                    vps.append(buf,a1);
                    break;
                case ENalType::H265_NAL_SPS:
                    sps.append(buf,a1);
                    break;
                case ENalType::H265_NAL_PPS:
                    pps.append(buf,a1);
                    break;
            }
        });
        LOGE("split ok h265:vps:%d,sps:%d,pps:%d",vps.size(),sps.size(),pps.size());
        ret = vps + sps + pps;
        return ret;
    }

}
