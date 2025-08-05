#include "SimpleAudioFormatConverter.h"

#include <vector>
#include "common/RData.h"

namespace tc
{

	std::shared_ptr<Data> SimpleAudioFormatConverter::CvtF32ToS16(std::shared_ptr<Data> origin) {
		return CvtF32ToS16((char*)origin->CStr(), origin->Size());
	}

	std::shared_ptr<Data> SimpleAudioFormatConverter::CvtF32ToS16(char* origin, int origin_size) {
		int s32_size = origin_size / sizeof(int);
		auto f32_data = (float*)origin;
		std::vector<int16_t> result;
		for (int i = 0; i < s32_size; i++) {
			int16_t value = f32_data[i] * 32768;
			result.push_back(value);
		}
		return Data::Make((char*)result.data(), result.size() * 2);
	}

}