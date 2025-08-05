#pragma once

#include <memory>

namespace tc
{

	class Data;

	enum class SimpleAudioFormat {
		kPCM_S16,
		kPCM_F32,
	};

	class SimpleAudioFormatConverter {
	public:

		static std::shared_ptr<Data> CvtF32ToS16(std::shared_ptr<Data> origin);
		static std::shared_ptr<Data> CvtF32ToS16(char* origin, int origin_size);

	};

}