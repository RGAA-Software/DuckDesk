#include "audio_source_imp.h"
#include <iostream>
#include "tc_common_new/log.h"

namespace tc
{

void AudioSourceImp::SendAudio(dlca_webrtc_frame* frame)
{
	//const int kBytePerSample = frame->bits_per_sample / 8;
	//// 每次发 10ms数据。
	//const int k10msNumPerSecond = 100;
	//int perSendSampleCount = frame->sample_rate / k10msNumPerSecond;
	//int perSendSize = perSendSampleCount * kBytePerSample * frame->channel_num;
	//if(perSendSize == 0)
	//{
	//	LOGI("send audio frame per send size is 0");
	//	return;
	//}
	//int sendCount = frame->size / perSendSize;
	//std::lock_guard<std::mutex> lock(mSinkLock);
	//for(int i = 0; i < sendCount; ++i)
	//{
	//	for (auto sink : mSinks)
	//	{
	//		sink->OnData(frame->data + perSendSize * i, frame->bits_per_sample, frame->sample_rate, frame->channel_num,perSendSampleCount);
	//	}
	//}
}

} // namespace dl