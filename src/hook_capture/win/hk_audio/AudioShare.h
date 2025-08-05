#pragma once

#include <memory>
#include "SimpleAudioFormatConverter.h"

namespace tc
{

	class Data;
	class Thread;

	class AudioShare {
	public:

		AudioShare();
		~AudioShare();
		
		void SetAudioFormat(SimpleAudioFormat format);
		void PostAudioData(std::shared_ptr<Data> data);

	private:
	
		std::shared_ptr<Thread> share_thread = nullptr;
		SimpleAudioFormat format;

	};

}