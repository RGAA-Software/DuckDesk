#pragma once

//#include <WinSock2.h>

#include <mutex>
#include <memory>
#include "shared/d3drender.h"
#include "VideoEncoder.h"
#include "NvEncoderD3D11.h"
#include <fstream>
//#include "NvEncoderCuda.h"
//#include "CudaConverter.h"
//#include "ipctools.h"
#if 1
#include <condition_variable>
#include "EncodeDataCallback.h"
#include "VideoEncoder.h"
#define H264_TYPE(v) ((uint8_t)(v) & 0x1F)


class VideoEncoderNVENC : public LegacyVideoEncoder
{
public:
	VideoEncoderNVENC(std::shared_ptr<CD3DRender> pD3DRender, EncodeDataCallback data_cbk, bool useNV12, int width, int height);
	~VideoEncoderNVENC();

	bool Initialize();
	void Shutdown();

	void Transmit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D *texture, uint64_t frame_index, bool insertIDR);
	void Transmit(uint64_t handle, uint64_t frame_index) override;
	bool InitEncoder(ID3D11Texture2D* texture) override;

private:

	void EncodeTextureHandle(uint64_t handle, uint64_t frame_index);

private:
	std::ofstream fpOut;
	std::shared_ptr<NvEncoder> m_NvNecoder = nullptr;

	std::shared_ptr<CD3DRender> m_pD3DRender = nullptr;
	int m_nFrame;

	NV_ENC_BUFFER_FORMAT format = NV_ENC_BUFFER_FORMAT_UNDEFINED;
	
	bool m_useNV12;
	bool init = false;

};

#endif