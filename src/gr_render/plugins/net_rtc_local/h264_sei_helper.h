#pragma once

#include <string>
#include <optional>

namespace tc
{

    struct SeiInfo {
        // frame idx
        uint16_t frame_index_{ 0 };
        // 发送者的时间
        uint64_t sender_ts_{ 0 };

        std::string AsString() {
            std::string buffer;
            buffer.resize(sizeof(SeiInfo));
            memcpy(buffer.data(), this, buffer.size());
            return buffer;
        }

    };

    class H264SeiHelper {
    public:
        // 生成一个自己的SEI NAL单元
        static std::string GenCustomSei(const std::string& data);
        static std::optional<SeiInfo> ParseCustomSei(const uint8_t* buffer, size_t size);
    };

}