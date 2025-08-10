#include "rtc_shared_video_encoder.h"
#include "tc_common_new/log.h"
#include "tc_common_new/time_util.h"
#include "h264_sei_helper.h"

namespace tc
{

bool gAdapterBitrate = false;
namespace
{

std::list<RtcSharedVideoEncoder*> sAllVideoEncoder;
std::mutex sAllVideoEncoderMutex;
} // namespace

RtcSharedVideoEncoder::RtcSharedVideoEncoder(RtcLocalPlugin* plugin) {
	RegisterEvents();
}

RtcSharedVideoEncoder::~RtcSharedVideoEncoder() {
	LOGI("rtc shared encoder released.");
}

int RtcSharedVideoEncoder::GetAllVideoEncoderMinBitrate()
{
	std::lock_guard<std::mutex> lk(sAllVideoEncoderMutex);
	int min = std::numeric_limits<int>::max();
	for (RtcSharedVideoEncoder* encoder : sAllVideoEncoder)
	{
		int bitrat = encoder->GetTargetBitrate();
		if (bitrat == 0)
			continue;
		if (bitrat < min)
			min = bitrat;
	}
	if(min != std::numeric_limits<int>::max())
		return min;
	return 0;
}

int RtcSharedVideoEncoder::GetVideoEncoderCount()
{
	std::lock_guard<std::mutex> lk(sAllVideoEncoderMutex);
	return sAllVideoEncoder.size();
}

int32_t RtcSharedVideoEncoder::InitEncode(const webrtc::VideoCodec* codec_settings, const webrtc::VideoEncoder::Settings& settings) 
{	
	std::lock_guard<std::mutex> lk(sAllVideoEncoderMutex);
	sAllVideoEncoder.push_back(this);
	encoder_index_ = sAllVideoEncoder.size();
	LOGI("InitEncode start bitrate {} kbps", codec_settings->startBitrate);
	return WEBRTC_VIDEO_CODEC_OK;
}

int32_t RtcSharedVideoEncoder::Release() 
{
	std::lock_guard<std::mutex> lk(sAllVideoEncoderMutex);
	sAllVideoEncoder.remove(this);
	return WEBRTC_VIDEO_CODEC_OK;
}

void RtcSharedVideoEncoder::SetRates(const RateControlParameters& parameters)
{
	std::stringstream ss;
	ss << "rtc update bitrate:" << parameters.bitrate.get_sum_kbps();
	ss << " kbps target bitrat:" << parameters.target_bitrate.get_sum_kbps();
	ss << " kbps bandwidth_allocation:" << parameters.bandwidth_allocation.kbps();
	ss << " kbps fps:" << parameters.framerate_fps;
    LOGI("SetRates: {}", ss.str());
	mTargetBitrate = parameters.bitrate.get_sum_bps();
}

int32_t RtcSharedVideoEncoder::Encode(const webrtc::VideoFrame& frame, const std::vector<webrtc::VideoFrameType>* frame_types) 
{
    LOGI("Rtc encoder: Encode");
	if (!mEncodedImageCallback) {
		RTC_LOG(LS_WARNING) << "RegisterEncodeCompleteCallback() not called";
		return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
	}
	
	mWidth = frame.width();
	mHeight = frame.height();
	rtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer = frame.video_frame_buffer();
	if (buffer->type() != webrtc::VideoFrameBuffer::Type::kNative) {
		RTC_LOG(LS_WARNING) << "buffer type must be kNative";
		return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
	}

	bool insert_idr = false;
	if (frame_types)
	{
		if (!frame_types->empty())
		{
			if (frame_types->at(0) == webrtc::VideoFrameType::kVideoFrameKey)
			{
				LOGI("insert i frame due to rtc req i frame.");
				insert_idr = true;
			}
		}
	}

	struct PassArgs
	{
		webrtc::EncodedImageCallback* Callback;
		int64_t NtpTimeMs;
		uint32_t Timestamp;
		RtcSharedVideoEncoder* thiz;
		uint16_t frame_id_;
	} args;

	args.Callback = mEncodedImageCallback;
	args.NtpTimeMs = frame.ntp_time_ms();
	args.Timestamp = frame.timestamp();
	args.thiz = this;
	args.frame_id_ = frame.id();

//	auto res = GEncodeFrameSignal(insert_idr, frame.id(), [](void* user_data, dlca_webrtc_frame* encoded_frame) {
//		PassArgs* args = (PassArgs*)user_data;
//
//		if (args->thiz->last_encoded_frame_seq_) {
//			if(encoded_frame->key_frame == 0) {
//				// 帧序列检查。
//				// P帧重复
//				if (args->thiz->last_encoded_frame_seq_ == encoded_frame->frame_index) {
//					LogW(kLogRtc,"video encoder index {} dup p frame. igonre it seq {}",
//						args->thiz->encoder_index_,encoded_frame->frame_index);
//					return;
//				}
//				// 不是上一次连续的P帧。
//				if(uint16_t(*args->thiz->last_encoded_frame_seq_ + 1) != encoded_frame->frame_index) {
//					LogW(kLogRtc,"video encoder index {} detect encoded seq incontinuity, last encoded seq {} current seq {}",
//						args->thiz->encoder_index_,*args->thiz->last_encoded_frame_seq_,encoded_frame->frame_index);
//					GOnNeedInsertIDRDelegate.ExecuteIfBound();
//					return;
//				}
//			}
//		}
//		webrtc::EncodedImageCallback* encodedImageCallback = args->Callback;
//		webrtc::EncodedImage encodedImage;
//
//		if (args->thiz->insert_timer_sei_) {
//			args->thiz->insert_timer_sei_ = false;
//			SeiInfo sei_info;
//			sei_info.frame_index_ = args->frame_id_;// encoded_frame->frame_index;
//			sei_info.sender_ts_ = TimeUtil::GetCurrentTimestamp();;
//			auto sei = H264SeiHelper::GenCustomSei(sei_info.AsString());
//			std::string target_buffer;
//			target_buffer.resize(sei.size() + encoded_frame->size);
//			memcpy(target_buffer.data(), sei.data(), sei.size());
//			memcpy(target_buffer.data() + sei.size(), encoded_frame->data, encoded_frame->size);
//			encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create((uint8_t*)target_buffer.data(), target_buffer.size()));
//			//RLogI("Insert timer sei,frame id: {}, frame index: {} ts: {}", sei_info.frame_index_, encoded_frame->frame_index, sei_info.sender_ts_);
//
//			//RtcTimePoint tp;
//			//tp.frame_idx_ = sei_info.frame_index_;
//			//tp.timestamp_ = sei_info.sender_ts_;
//			//args->thiz->rtc_server_stat_->AppendSendTimePoint(tp);
//		}
//		else {
//			encodedImage.SetEncodedData(webrtc::EncodedImageBuffer::Create(encoded_frame->data, encoded_frame->size));
//		}
//		encodedImage.SetRtpTimestamp(args->Timestamp);
//		encodedImage.ntp_time_ms_ = args->NtpTimeMs;
//		encodedImage._frameType = encoded_frame->key_frame ? webrtc::VideoFrameType::kVideoFrameKey : webrtc::VideoFrameType::kVideoFrameDelta;
//		encodedImage._encodedWidth = encoded_frame->width;
//		encodedImage._encodedHeight = encoded_frame->height;
//		webrtc::CodecSpecificInfo codec_specific;
//		codec_specific.codecType = webrtc::VideoCodecType::kVideoCodecH264;
//		encodedImageCallback->OnEncodedImage(encodedImage, &codec_specific);
//		args->thiz->last_encoded_frame_seq_ = encoded_frame->frame_index;
//	}, &args);

    return WEBRTC_VIDEO_CODEC_OK;
}

webrtc::VideoEncoder::EncoderInfo RtcSharedVideoEncoder::GetEncoderInfo() const 
{
	webrtc::VideoEncoder::EncoderInfo info;
	info.implementation_name = "NullEncoder";
	info.supports_native_handle = true;
	info.is_hardware_accelerated = true;
	info.has_trusted_rate_controller = false;
	//info.has_internal_source = false;
	info.supports_simulcast = false;
	int min,max;
	if (gAdapterBitrate)
	{
        constexpr auto kDefalutMinBitrate = 1 * 1024 * 1024;
        constexpr auto kDefalutStartBitrate = 15 * 1024 * 1024;
        constexpr auto kDefalutMaxBitrate = 100 * 1024 * 1024;
		min = kDefalutMinBitrate;
		max = kDefalutMaxBitrate;
	}
	else
	{
		min = 128 * 1024 * 1024;
		max = 256 * 1024 * 1024;
	}
	int start = max;
	info.resolution_bitrate_limits.push_back(ResolutionBitrateLimits(1 * 1, start, min, max));
	info.resolution_bitrate_limits.push_back(ResolutionBitrateLimits(8192 * 8192, start, min, max));
	return info;
}

void RtcSharedVideoEncoder::OnPacketLossRateUpdate(float packet_loss_rate)
{
	if (packet_loss_rate > 0.01) {
		LOGI("rtc packet loss update: {}", packet_loss_rate);
	}
}

// Inform the encoder when the round trip time changes.
//
// Input:   - rtt_ms            : The new RTT, in milliseconds.
void RtcSharedVideoEncoder::OnRttUpdate(int64_t rtt_ms)
{
	// LOG(kLogLevelInformation) << "rtc rtt update:" << rtt_ms;
}

int RtcSharedVideoEncoder::GetMaxBitrate() const
{
	//return GWebrtcConfig.max_bitrate * (float(mWidth) * mHeight / (1920 * 1080));
    return 0;
}

void RtcSharedVideoEncoder::RegisterEvents() {
	//msg_listener_ = rtc_ctx_->GetMessageNotifier()->CreateListener();
	//// 2S定时器
	//msg_listener_->Listen<SigEvtTimer1S>([=](const SigEvtTimer1S& evt) {
	//	insert_timer_sei_ = true;
	//});
}

} // namespace dl