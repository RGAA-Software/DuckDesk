#include "VideoEncoderFFmpeg.h"

#include "common/RData.h"
#include "common/RLog.h"

#include <fstream>

namespace tc {

	VideoEncoderFFmpeg::VideoEncoderFFmpeg(Running::EncoderType type, int width, int height, EncodeDataCallback data_cbk) : VideoEncoder(type) {
		m_width = width;
		m_height = height;
		m_Listener = data_cbk;
	}
	
	VideoEncoderFFmpeg::~VideoEncoderFFmpeg() {
	 
	}

	bool VideoEncoderFFmpeg::Initialize() {
		
		//���ñ�����
		const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_H265);

		//��ʼ�������ñ�����������
		context = avcodec_alloc_context3(encoder);
		if (!context) {
			LOG_ERROR("avcodec_alloc_context3 error!");
			return false;
		}
		context->width = m_width;
		context->height = m_height;
		context->time_base = { 1, 60 };
		context->pix_fmt = AV_PIX_FMT_YUV420P;
		context->thread_count = 4;

		//��������ʼ��
		auto ret = avcodec_open2(context, encoder, NULL);
		if (ret != 0) {
			LOG_ERROR("avcodec_open2 error !");
			return false;
		}

		//��ʼ�������� AV FRAME
		frame = av_frame_alloc();
		frame->width = context->width;
		frame->height = context->height;
		frame->format = context->pix_fmt;

		//ΪAV FRAME���仺����
		av_frame_get_buffer(frame, 0);

		LOG_INFO("Line 1: %d 2: %d 3: %d", frame->linesize[0], frame->linesize[1], frame->linesize[2]);

		//��ʼ��AV PACKET
		packet = av_packet_alloc();

		return true;
	}

	void VideoEncoderFFmpeg::Shutdown() {
		
	}

	//std::ofstream outputFile("1.video.h265", std::ios::binary);

	void VideoEncoderFFmpeg::Transmit(std::shared_ptr<Data> i420_data, uint64_t frame_index) {
		frame->pts = frame_index;		

		// �������
		int y_size = m_width * m_height;
		int uv_size = m_width * m_height / 4;
		memcpy(frame->data[0], i420_data->CStr(), y_size);
		memcpy(frame->data[1], i420_data->CStr() + y_size, uv_size);
		memcpy(frame->data[2], i420_data->CStr() + y_size + uv_size, uv_size);

		int sendResult = avcodec_send_frame(context, frame);

		while (sendResult >= 0)
		{
			//��ȡתΪH264��packet����
			int receiveResult = avcodec_receive_packet(context, packet);
		
			//������޴�����ɵ�packet���ݣ��������������YUV֡
			if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF) break;
			
			//outputFile.write((char*)packet->data, packet->size);
			
			auto encoded_data = Data::Make((char*)packet->data, packet->size);
			if (m_Listener) {
				m_Listener(encoded_data, frame_index, false);
			}
			//�ͷ�packet�е�����
			av_packet_unref(packet);
		}
	}

}