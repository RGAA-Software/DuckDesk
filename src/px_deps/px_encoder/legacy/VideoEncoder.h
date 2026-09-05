#pragma once

#include <functional>
#include <memory>
#include "shared/d3drender.h" 
#include "EncodeDataCallback.h"
#include "px_common/time_util.h"
#include "px_encoder/encoder_config.h"

typedef std::function<void(bool)> OnInitEncoderCallback;

using namespace px;

namespace px {
	class Data;
}

class LegacyVideoEncoder
{
public:
    LegacyVideoEncoder(EVideoCodecType codec);

	virtual bool Initialize() = 0;
	virtual void Shutdown() = 0;
	virtual bool InitEncoder(ID3D11Texture2D* texture) = 0;
	virtual void Transmit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D *pTexture, uint64_t frameIndex, bool insertIDR);
	virtual void Transmit(uint64_t handle, uint64_t frame_index);
	virtual void Transmit(std::shared_ptr<Data> data, uint64_t frame_index);

	void InsertIDR();
	void SetInitCallback(OnInitEncoderCallback);

private:
	void CalculateFPS();

protected:

	virtual void OnInitSuccess();
	virtual void OnInitFailed();

protected:
	int gop = 60;
	bool insert_idr = false;

	int m_width;
	int m_height;
	EncodeDataCallback m_Listener;
	OnInitEncoderCallback init_callback;

	int fps = 0;
	uint64_t last_update_time = TimeUtil::GetCurrentTimestamp();
    EVideoCodecType codec_type_;
};