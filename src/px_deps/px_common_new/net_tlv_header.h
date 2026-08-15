//
// Created by RGAA on 16/04/2025.
//

#ifndef GAMMARAY_NET_TLV_HEADER_H
#define GAMMARAY_NET_TLV_HEADER_H

namespace tc
{

    constexpr uint8_t kNetTlvFull = 0x01;
    constexpr uint8_t kNetTlvBegin = 0x02;
    constexpr uint8_t kNetTlvCenter = 0x03;
    constexpr uint8_t kNetTlvEnd = 0x04;

    // KB
    constexpr uint32_t kSplitBufferSize = 128*1024;

    struct NetTlvHeader {
        uint32_t type_ = 0;
        uint32_t this_buffer_length_ = 0;
        //
        uint32_t this_buffer_begin_ = 0;
        //
        uint32_t this_buffer_end_ = 0;
        // index
        uint64_t pkt_index_ = 0;
        // parent buffer
        uint32_t parent_buffer_length_ = 0;
        //
        //uint8_t* buffer_;
    };

}

#endif //GAMMARAY_NET_TLV_HEADER_H
