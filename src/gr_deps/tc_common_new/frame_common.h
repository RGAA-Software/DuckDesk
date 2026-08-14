#pragma once

#define H264_TYPE(v) ((uint8_t)(v) & 0x1F)
#define H265_TYPE(v) (((uint8_t)(v) >> 1) & 0x3f)

namespace tc {

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

}
