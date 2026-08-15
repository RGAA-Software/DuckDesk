#include "VideoEncoderVP9.h"

#include "common/RLog.h"
#include "common/RData.h"
#include "common/RTime.h"
#include "D3DTextureDebug.h"

#include <dxgi.h>
#include <d3d11.h>
#include <atlbase.h>
#include <windows.h>

#include <fstream>
#include <chrono>
#include <algorithm>

namespace tc
{

	static VpxInterface vpx_encoders[] = {
	#if CONFIG_VP8_ENCODER
	  { "vp8", VP8_FOURCC, &vpx_codec_vp8_cx },
	#endif

	#if CONFIG_VP9_ENCODER
	  { "vp9", VP9_FOURCC, &vpx_codec_vp9_cx },
	#endif
	};

	static int get_vpx_encoder_count(void) {
		return sizeof(vpx_encoders) / sizeof(vpx_encoders[0]);
	}

	static VpxInterface* get_vpx_encoder_by_index(int i) { 
		return &vpx_encoders[i]; 
	}

	static VpxInterface* get_vpx_encoder_by_name(const char* name) {
		int i;

		for (i = 0; i < get_vpx_encoder_count(); ++i) {
			VpxInterface* encoder = get_vpx_encoder_by_index(i);
			if (strcmp(encoder->name, name) == 0) return encoder;
		}

		return NULL;
	}

	//static int encode_frame(vpx_codec_ctx_t* codec, vpx_image_t* img, int frame_index, int flags) {
	//	int got_pkts = 0;
	//	vpx_codec_iter_t iter = NULL;
	//	const vpx_codec_cx_pkt_t* pkt = NULL;
	//	const vpx_codec_err_t res = vpx_codec_encode(codec, img, frame_index, 1, flags, VPX_DL_GOOD_QUALITY);
	//	if (res != VPX_CODEC_OK) { 
	//		LOG_INFO("Failed to encode frame"); 
	//	}
	//
	//	while ((pkt = vpx_codec_get_cx_data(codec, &iter)) != NULL) {
	//		got_pkts = 1;
	//
	//		if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
	//			const int keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
	//			if (!vpx_video_writer_write_frame(writer, pkt->data.frame.buf,
	//				pkt->data.frame.sz,
	//				pkt->data.frame.pts)) {
	//				LOG_INFO("Failed to write compressed frame");
	//			}
	//			printf(keyframe ? "K" : ".");
	//			//fflush(stdout);
	//		}
	//	}
	//
	//	return got_pkts;
	//}

	VideoEncoderVP9::VideoEncoderVP9(int width, int height, EncodeDataCallback data_cbk) : VideoEncoder(Running::kEncodeVP9) {
		this->width = width;
		this->height = height;
		this->m_Listener = data_cbk;

		d3d_render = std::make_shared<CD3DRender>();
		d3d_render->Initialize(0);
	}

	VideoEncoderVP9::~VideoEncoderVP9() {

	}

	bool VideoEncoderVP9::Initialize() {

		return true;
	}

	void VideoEncoderVP9::Shutdown() {
		if (!encoder) {
			return;
		}
		vpx_img_free(&raw);
		if (vpx_codec_destroy(&codec)) { 
			LOG_INFO("Failed to destroy codec."); 
		}
	}

#if 0
	bool VideoEncoderVP9::InitD3DDevice() {
		UINT creationFlags = 0;
#if _DEBUG
		creationFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
		D3D_FEATURE_LEVEL eFeatureLevel;
		D3D_FEATURE_LEVEL support_feature_level[] = {
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_11_1,
		};
		int support_feature_level_size = sizeof(support_feature_level) / sizeof(support_feature_level[0]);
		HRESULT hRes = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, creationFlags, support_feature_level, support_feature_level_size, D3D11_SDK_VERSION, &device, &eFeatureLevel, &context);
		if (FAILED(hRes)) {
			LOG_INFO("Failed D3D11CreateDevice at VideoEncoderVP9 : 0x%x", hRes);
			return false;
		}
		LOG_INFO("Init D3DDevice at VideoEncoderVP9 success.");
		return true;
	}
#endif

	bool VideoEncoderVP9::InitEncoder(ID3D11Texture2D* texture) {
		encoder = get_vpx_encoder_by_name("vp9");
		if (!encoder) {
			LOG_INFO("Can't find vp9 encoder.");
			OnInitFailed();
			return false;
		}

		VpxVideoInfo info = { 0, 0, 0, { 0, 0 } };
		info.codec_fourcc = encoder->fourcc;
		info.frame_width = width;
		info.frame_height = height;
		info.time_base.numerator = 1;
		info.time_base.denominator = fps;

		if (!vpx_img_alloc(&raw, VPX_IMG_FMT_I420, info.frame_width, info.frame_height, 1)) {
			OnInitFailed();
			LOG_INFO("Failed to allocate image.");
			return false;
		}
		LOG_INFO("Raw : %dx%d, bit_depth : %d", raw.w, raw.h, raw.bit_depth);

		printf("Using %s\n", vpx_codec_iface_name(encoder->codec_interface()));

		res = vpx_codec_enc_config_default(encoder->codec_interface(), &cfg, 0);
		if (res) {
			OnInitFailed();
			LOG_INFO("Failed to get default codec config.");
			return false;
		}

		cfg.g_w = info.frame_width;
		cfg.g_h = info.frame_height;
		cfg.g_timebase.num = info.time_base.numerator;
		cfg.g_timebase.den = info.time_base.denominator;
		cfg.rc_target_bitrate = bitrate;
		cfg.rc_undershoot_pct = 95;
		cfg.rc_dropframe_thresh = 25;
		cfg.g_error_resilient = VPX_ERROR_RESILIENT_DEFAULT;// (vpx_codec_er_flags_t)strtoul(argv[7], NULL, 0);
		cfg.g_threads = 8;
		cfg.rc_end_usage = vpx_rc_mode::VPX_CBR;
	  	cfg.kf_mode = vpx_kf_mode::VPX_KF_DISABLED;

		if (vpx_codec_enc_init(&codec, encoder->codec_interface(), &cfg, 0)) {
			OnInitFailed();
			LOG_INFO("Failed to initialize encoder");
			return false;
		}

		vpx_codec_control(&codec, VP8E_SET_CPUUSED, 7);
		vpx_codec_control(&codec, VP9E_SET_ROW_MT, 1);
		vpx_codec_control(&codec, VP9E_SET_TILE_COLUMNS, 4);

		OnInitSuccess();
		LOG_INFO("Init encoder success..");

		//InitD3DDevice();
		StartEncoderThread();
		return true;
	}

	void VideoEncoderVP9::StartEncoderThread() {
		encoder_thread = std::thread(std::bind(&VideoEncoderVP9::EncoderRun, this));
	}

	void VideoEncoderVP9::EncoderRun() {
		last_time = tc::GetCurrentTimestamp();
		for (;;) {
			auto get_ready_idx = [=]() -> int {
				if (texture_size != textures.size()) {
					return -1;
				}
				for (int i = 0; i < texture_size; i++) {
					auto& tex = textures.at(i);
					if (tex.ready) {
						return i;
					}
				}
				return -1;
			};
			
			int ready_idx = -1;
			{
				std::lock_guard<std::mutex> lock(cache_tex_mtx);
				ready_idx = get_ready_idx();
			}
			
			{
				std::unique_lock<std::mutex> guard(cache_tex_cv_mtx);
				cache_tex_cv.wait(guard, [=]() {
					return new_frame_arrived || ready_idx > -1;
				});
				if (new_frame_arrived) {
					new_frame_arrived = false;
				}
			}

			auto begin = GetCurrentTimestamp();
			CComPtr<ID3D11Texture2D> texture = nullptr;
			uint64_t frame_index;
			bool key_frame = false;
			{
				std::lock_guard<std::mutex> lock(cache_tex_mtx);

				std::sort(textures.begin(), textures.end(), [](CacheTexture& l, CacheTexture& r) {
					return l.frame_index < r.frame_index;
				});

				//for (auto& t : textures) {
				//	LOG_INFO("t.frame_index : %d", t.frame_index);
				//}

				ready_idx = get_ready_idx();
				if (ready_idx != -1) {
					CacheTexture& tex = textures.at(ready_idx);
					texture = tex.texture;
					frame_index = tex.frame_index;
					key_frame = tex.key_frame;

					tex.ready = false;
					tex.frame_index = 0;
					tex.key_frame = false;
				}


				if (ready_idx == -1 || !texture) {
					std::this_thread::sleep_for(std::chrono::milliseconds(1));
					continue;
				}
				EncoderInternal(texture, frame_index, key_frame);
			}
		}
	}

	void VideoEncoderVP9::EncoderInternal(ID3D11Texture2D* texture, uint64_t frame_idx, bool key_frame) {
		encode_fps++;
		uint64_t current_time = tc::GetCurrentTimestamp();
		if (current_time - last_time > 1000) {
			printf("Encode FPS : %d \n", encode_fps);
			last_time = current_time;
			encode_fps = 0;
		}

		int encode_flags = 0;
		if (key_frame) {
			encode_flags |= VPX_EFLAG_FORCE_KF;
		}
		auto begin = GetCurrentTimestamp();
		D3D11_TEXTURE2D_DESC desc;
		texture->GetDesc(&desc);
		CComPtr<IDXGISurface> staging_surface = nullptr;
		HRESULT hr = texture->QueryInterface(IID_PPV_ARGS(&staging_surface));

		if (FAILED(hr)) {
			LOG_INFO("!QueryInterface(IDXGISurface), #0x%08X\n", hr);
			return;
		}

		DXGI_MAPPED_RECT mapped_rect{};
		hr = staging_surface->Map(&mapped_rect, DXGI_MAP_READ);
		if (FAILED(hr)) {
			LOG_INFO("!Map(IDXGISurface), #0x%08X\n", hr);
			return;
		}

		size_t pixel_size = width * height;

		const int uv_stride = width >> 1;
		int rgba_size = width * height * 4;
		if (yuv_frame_data.size() != rgba_size) {
			yuv_frame_data.resize(rgba_size);
		}
		uint8_t* y = yuv_frame_data.data();
		uint8_t* u = y + pixel_size;
		uint8_t* v = u + (pixel_size >> 2);

		if (DXGI_FORMAT_R8G8B8A8_UNORM == desc.Format) {
			libyuv::ABGRToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
		}
		else if (DXGI_FORMAT_B8G8R8A8_UNORM == desc.Format) {
			libyuv::ARGBToI420(mapped_rect.pBits, mapped_rect.Pitch, y, width, u, uv_stride, v, uv_stride, width, height);
		}
		else {
			staging_surface->Unmap();
			LOG_INFO("Unsupported format %u.", desc.Format);
			return;
		}

		{
			static bool save_yuv = false;
			if (!save_yuv) {
				std::ofstream out("abc.yuv", std::ios::binary);
				out.write((char*)yuv_frame_data.data(), width * height * 1.5);
				out.close();
				save_yuv = true;
			}
		}

		staging_surface->Unmap();

		memcpy(raw.planes[0], y, pixel_size);
		memcpy(raw.planes[1], u, pixel_size / 4);
		memcpy(raw.planes[2], v, pixel_size / 4);

		int got_pkts = 0;
		vpx_codec_iter_t iter = NULL;
		const vpx_codec_cx_pkt_t* pkt = NULL;
		vpx_codec_err_t res = vpx_codec_encode(&codec, &raw, frame_idx, 1, encode_flags, VPX_DL_REALTIME);
		if (res != VPX_CODEC_OK) {
			LOG_INFO("Failed to encode frame");
		}

		while ((pkt = vpx_codec_get_cx_data(&codec, &iter)) != NULL) {
			got_pkts = 1;
			if (pkt->kind == VPX_CODEC_CX_FRAME_PKT) {
				const int keyframe = (pkt->data.frame.flags & VPX_FRAME_IS_KEY) != 0;
				auto data = Data::Make((char*)pkt->data.frame.buf, pkt->data.frame.sz);
				//LOG_INFO("encode data size : %d", data->Size());
				//printf("vp9 encoded size : %d\n", data->Size());
				if (m_Listener) {
					m_Listener(data, frame_idx, keyframe);
				}
				if (keyframe) {
					encode_flags = 0;
					printf("K");
				}
			}
		}

		auto end = GetCurrentTimestamp();
		//LOG_INFO("index : %d, duration : %d, frame index : %d ", 0, (end - begin), frame_idx);

	}

	void VideoEncoderVP9::Transmit(uint64_t handle, uint64_t frame_index) {
		CComPtr<ID3D11Texture2D> target_texture = NULL;
		if (FAILED(d3d_render->GetDevice()->OpenSharedResource((HANDLE)handle, __uuidof(ID3D11Texture2D), (void**)&target_texture))) {
			LOG_ERROR("OpenSharedResource failed !");
			return;
		}

		//todo: check params , re-create the encoder...

		if (!encoder) {
			InitEncoder(target_texture);
			if (!encoder) {
				return;
			}
		}
		
		if (!insert_idr) {
			if (frame_index % gop == 0) {
				insert_idr = true;
			}
		}
		Transmit(d3d_render->GetDevice(), d3d_render->GetContext(), target_texture, frame_index, insert_idr);
		insert_idr = false;
	}

	void VideoEncoderVP9::Transmit(ID3D11Device* device, ID3D11DeviceContext* ctx, ID3D11Texture2D* pTexture, uint64_t frameIndex, bool insertIDR) {
		if (textures.empty()) {
			D3D11_TEXTURE2D_DESC tex_desc;
			pTexture->GetDesc(&tex_desc);
			for (int i = 0; i < texture_size; i++) {
				D3D11_TEXTURE2D_DESC desc = {};
				desc.Width = tex_desc.Width;
				desc.Height = tex_desc.Height;
				desc.Format = tex_desc.Format;
				desc.MipLevels = 1;
				desc.ArraySize = 1;
				desc.SampleDesc.Count = 1;
				desc.Usage = D3D11_USAGE_STAGING;
				desc.BindFlags = 0;
				desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
				desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

				CComPtr<ID3D11Texture2D> texture = nullptr;
				// ??? text_desc ??? desc û���ϲ���
				auto hr = device->CreateTexture2D(&tex_desc, nullptr, &texture);
				if (FAILED(hr)) {
					printf("create_d3d11_stage_surface: failed to create texture 0x%x \n", hr);
					return;
				}
				LOG_INFO("CreateTexture2D in VideoEncoderVP9 success.");

				CacheTexture ct;
				ct.texture = texture;
				ct.ready = false;
				textures.push_back(ct);
			}
		}
		
		{
			std::lock_guard<std::mutex> lock(cache_tex_mtx);
			int idx = texture_index % texture_size;
			ctx->CopyResource(textures[idx].texture, pTexture);
			textures[idx].ready = true;
			textures[idx].frame_index = frameIndex;
			if (keyframe_interval > 0 && frameIndex % keyframe_interval == 0) {
				textures[idx].key_frame = true;
			}
			texture_index++;
		}

		{
			std::lock_guard<std::mutex> guard(cache_tex_cv_mtx);
			new_frame_arrived = true;
			cache_tex_cv.notify_one();
		}
 
	}

}