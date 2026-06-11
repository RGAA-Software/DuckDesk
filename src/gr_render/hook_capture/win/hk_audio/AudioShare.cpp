#include "AudioShare.h"

#include "common/RData.h"
#include "common/RThread.h"
#include "ipc/IPCMessage.h"
#include "ipc/InterCommClient.h"

namespace tc
{

	AudioShare::AudioShare() {
		share_thread = Thread::Make("share_audio_thread", 128);
		share_thread->Poll();
	}

	AudioShare::~AudioShare() {

	}

	void AudioShare::SetAudioFormat(SimpleAudioFormat format) {
		this->format = format;
	}

	void AudioShare::PostAudioData(std::shared_ptr<Data> data) {
		share_thread->Post(SimpleThreadTask::Make([=]() {
			auto pcm_s16 = data;
			if (this->format == SimpleAudioFormat::kPCM_F32) {
				pcm_s16 = SimpleAudioFormatConverter::CvtF32ToS16(data);
			}

			auto send_msg = Data::Make(nullptr, sizeof(IPCAudioDataMessage) + pcm_s16->Size());

			IPCAudioDataMessage message;
			message.type = kSharedAudioData;
			message.sender = IPCMessageSender::kSenderClient;
			message.buf_len = pcm_s16->Size();
			memcpy(send_msg->DataAddr(), (char*)&message, sizeof(IPCAudioDataMessage));
			memcpy(send_msg->DataAddr() + sizeof(IPCAudioDataMessage), pcm_s16->CStr(), pcm_s16->Size());

			InterCommClient::Instance()->SendBack(send_msg);

		}, []() {}));
	}

}