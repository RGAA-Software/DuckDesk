#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dxgi.h>
#include <d3d11.h>
#include <atlbase.h>
#include <mutex>
#include <thread>
#include <condition_variable>

extern "C" 
{
#include <vpx/vpx_codec.h>
#include <vpx/vpx_encoder.h>
#include <vpx/vpx_image.h>
#include <vpx/vpx_integer.h>
#include <vpx/vp8cx.h>
}

#include <libyuv.h>

#include "VideoEncoder.h"
#include "EncodeDataCallback.h"
#include "shared/D3DRender.h"

#define CONFIG_VP9_ENCODER 1
#define VP8_FOURCC 0x30385056
#define VP9_FOURCC 0x30395056

namespace tc
{

	typedef struct VpxInterface {
		const char* const name;
		const uint32_t fourcc;
		vpx_codec_iface_t* (* const codec_interface)();
	} VpxInterface;

	struct VpxRational {
		int numerator;
		int denominator;
	};

	typedef struct {
		uint32_t codec_fourcc;
		int frame_width;
		int frame_height;
		struct VpxRational time_base;
	} VpxVideoInfo;

	typedef struct CacheTexture {
		CComPtr<ID3D11Texture2D> texture = nullptr;
		bool ready = false;
		uint64_t frame_index = 0;
		bool key_frame = false;
	} CacheTexture;

	class VideoEncoderVP9 : public VideoEncoder {
	public:
		VideoEncoderVP9(int width, int height, EncodeDataCallback data_cbk);
		~VideoEncoderVP9();

		bool Initialize() override;
		void Shutdown() override;

		void Transmit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* pTexture, uint64_t frameIndex, bool insertIDR) override;
		void Transmit(uint64_t handle, uint64_t frame_index);
		bool InitEncoder(ID3D11Texture2D* texture) override;

	private:

		bool InitD3DDevice();
		void StartEncoderThread();
		void EncoderRun();
		void EncoderInternal(ID3D11Texture2D* texture, uint64_t frame_idex, bool key_frame);

	private:
		int width = 0;
		int height = 0;
		VpxInterface* encoder = nullptr;

		vpx_image_t raw;
		vpx_codec_err_t res;
		vpx_codec_ctx_t codec;
		vpx_codec_enc_cfg_t cfg;

		const int fps = 60;
		int keyframe_interval = 60;
		int bitrate = 1500;
		std::vector<uint8_t> yuv_frame_data;

		uint64_t texture_index = 0;
		int texture_size = 2;
		std::vector<CacheTexture> textures;
		std::mutex cache_tex_mtx;

		bool new_frame_arrived = false;
		std::mutex cache_tex_cv_mtx;
		std::condition_variable cache_tex_cv;
		std::thread encoder_thread;

		uint64_t last_time = 0;
		int encode_fps = 0;

		std::shared_ptr<CD3DRender> d3d_render = nullptr;
	};

}