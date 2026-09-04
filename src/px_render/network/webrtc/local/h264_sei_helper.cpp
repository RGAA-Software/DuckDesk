#include "h264_sei_helper.h"
#include <sstream>
#include <array>

namespace px
{

    static std::array<unsigned char, 16> kSeiUuid = { 0x54, 0x8f, 0x83, 0x97, 0xf3, 0x23, 0x97, 0x4b, 0xb7, 0xc7, 0x4f, 0x3a, 0xb5, 0x6e, 0x89, 0x52 };
    static unsigned char kSeiUuidSize = 16;
    static unsigned char kSeiNalType = 0x06;
    static unsigned char kSeiPayloadType = 0x05;
    static unsigned char kSeiTail = 0x80;

    std::string H264SeiHelper::GenCustomSei(const std::string& data) {
        std::stringstream ss;
        // flag
        ss << (unsigned char)0 << (unsigned char)0 << (unsigned char)0 << (unsigned char)1;
        // nal type
        ss << kSeiNalType;
        // payload type
        ss << kSeiPayloadType;
        // payload size
        ss << (unsigned char)(kSeiUuidSize + data.size());
        // sei uuid
        for (const auto& c : kSeiUuid) {
            ss << c;
        }
        // data
        for (const auto& c : data) {
            ss << (unsigned char)c;
        }
        // tail
        ss << kSeiTail;
        return ss.str();
    }

    std::optional<SeiInfo> H264SeiHelper::ParseCustomSei(const uint8_t* buffer, size_t size) {
        // header
        uint8_t h0 = *(buffer + 0);
        uint8_t h1 = *(buffer + 1);
        uint8_t h2 = *(buffer + 2);
        uint8_t h3 = *(buffer + 3);
        if (h0 != 0 || h1 != 0 || h2 != 0 || h3 != 1) {
            return std::nullopt;
        }

        // nal type
        uint8_t nal_type = *(buffer + 4);
        if (nal_type != kSeiNalType) {
            return std::nullopt;
        }

        // payload type
        uint8_t payload_type = *(buffer + 5);
        if (payload_type != kSeiPayloadType) {
            return std::nullopt;
        }

        // payload size
        uint8_t payload_size = *(buffer + 6);
        if (payload_size <= kSeiUuidSize) {
            return std::nullopt;
        }

        auto data_size = payload_size - kSeiUuidSize;
        if (data_size != sizeof(SeiInfo)) {
            //LogE(kLogCommon,"Invalid sei size: {}, expect: {}", data_size, sizeof(SeiInfo));
            return std::nullopt;
        }

        // uuid
        uint8_t* uuid = (uint8_t*)malloc(kSeiUuidSize);
        memcpy(uuid, buffer+7, kSeiUuidSize);
        for (int i = 0; i < kSeiUuidSize; i++) {
            if (kSeiUuid[i] != *(uuid + i)) {
                //LogE(kLogCommon,"Invalid uuid at idx: {}, expect : 0x{:x}, but: 0x{:x}", i, kSeiUuid[i], *(uuid + i));
                return std::nullopt;
            }
        }

        uint8_t* data = (uint8_t*)malloc(data_size);
        memcpy(data, buffer+23, data_size);

        SeiInfo sei_info;
        memcpy(&sei_info, data, sizeof(SeiInfo));

        free(uuid);
        free(data);

        return sei_info;
    }

}