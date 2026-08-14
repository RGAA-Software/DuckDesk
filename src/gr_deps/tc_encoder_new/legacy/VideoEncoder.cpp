#include "VideoEncoder.h"
#include "tc_common_new/data.h"
#include "tc_common_new/log.h"

LegacyVideoEncoder::LegacyVideoEncoder(EVideoCodecType codec) {
    codec_type_ = codec;
}

void LegacyVideoEncoder::InsertIDR() {
	insert_idr = true;
}

void LegacyVideoEncoder::SetInitCallback(OnInitEncoderCallback cbk) {
	init_callback = cbk;
}

void LegacyVideoEncoder::OnInitSuccess() {
	if (init_callback) {
		init_callback(true);
	}
}

void LegacyVideoEncoder::OnInitFailed() {
	if (init_callback) {
		init_callback(false);
	}
}

void LegacyVideoEncoder::CalculateFPS() {
	auto current_time = TimeUtil::GetCurrentTimestamp();
	auto duration = current_time - last_update_time;
	fps++;
	if (duration > 1000) {
		LOGI("encode FPS: {}", fps);
		last_update_time = current_time;
		fps = 0;
	}
}

void LegacyVideoEncoder::Transmit(uint64_t handle, uint64_t frame_index) {
	CalculateFPS();
}

void LegacyVideoEncoder::Transmit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* pTexture, uint64_t frameIndex, bool insertIDR) {
	CalculateFPS();
}

void LegacyVideoEncoder::Transmit(std::shared_ptr<tc::Data> data, uint64_t frame_index) {
	CalculateFPS();
}