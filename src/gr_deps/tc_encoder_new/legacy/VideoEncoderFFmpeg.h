#pragma once

#include <libyuv.h>

#include "VideoEncoder.h"
#include "EncodeDataCallback.h"

extern "C" {
#include "libavcodec/avcodec.h"
}


namespace tc
{

	class VideoEncoderFFmpeg : public VideoEncoder {
	public:
		VideoEncoderFFmpeg(Running::EncoderType type, int width, int height, EncodeDataCallback data_cbk);
		~VideoEncoderFFmpeg();

		bool Initialize() override;
		void Shutdown() override;
		bool InitEncoder(ID3D11Texture2D* texture) override { return false; }
		void Transmit(std::shared_ptr<Data> i420, uint64_t frame_index) override;

	private:

		AVCodecContext* context = nullptr;
		AVFrame* frame = nullptr;
		AVPacket* packet = nullptr;
	};

}