#include "AMFEncoder.h"

#if 0

#define AMF_FACILITY L"SimpleEncoder"

namespace tc
{
	std::shared_ptr<AMFEncoder> AMFEncoder::Make(int width, int height) {
		return std::make_shared<AMFEncoder>(width, height);
	}

	AMFEncoder::AMFEncoder(int width, int height) {

	}

	AMFEncoder::~AMFEncoder() {

	}

	void AMFEncoder::SetTexture(ID3D11Texture2D* pTexture) {
		texture = pTexture;
	}

	int AMFEncoder::Start() {
#if 1
		AMF_RESULT res = AMF_OK; // error checking can be added later
		res = g_AMFFactory.Init();
		if (res != AMF_OK)
		{
			wprintf(L"AMF Failed to initialize");
			return 1;
		}

		::amf_increase_timer_precision();

		//amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_CONSOLE, true);
		//amf::AMFTraceEnableWriter(AMF_TRACE_WRITER_DEBUG_OUTPUT, true);

		// initialize AMF
		amf::AMFContextPtr context;
		amf::AMFComponentPtr encoder;
		amf::AMFSurfacePtr surfaceIn;

		// context
		res = g_AMFFactory.GetFactory()->CreateContext(&context);
		AMF_RETURN_IF_FAILED(res, L"CreateContext() failed");

		//if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
		//{
		//	res = amf::AMFContext1Ptr(context)->InitVulkan(NULL);
		//	AMF_RETURN_IF_FAILED(res, L"InitVulkan(NULL) failed");
		//	PrepareFillFromHost(context);
		//}
#ifdef _WIN32
		if (memoryTypeIn == amf::AMF_MEMORY_DX9)
		{

			res = context->InitDX9(NULL); // can be DX9 or DX9Ex device
			AMF_RETURN_IF_FAILED(res, L"InitDX9(NULL) failed");
		}
		else if (memoryTypeIn == amf::AMF_MEMORY_DX11)
		{
			res = context->InitDX11(NULL); // can be DX11 device
			AMF_RETURN_IF_FAILED(res, L"InitDX11(NULL) failed");
			//PrepareFillFromHost(context);
		}
#endif

		// component: encoder
		res = g_AMFFactory.GetFactory()->CreateComponent(context, pCodec, &encoder);
		AMF_RETURN_IF_FAILED(res, L"CreateComponent(%s) failed", pCodec);

		if (amf_wstring(pCodec) == amf_wstring(AMFVideoEncoderVCE_AVC))
		{
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_USAGE, AMF_VIDEO_ENCODER_USAGE_TRANSCODING) failed");

			if (bMaximumSpeed)
			{
				res = encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
				// do not check error for AMF_VIDEO_ENCODER_B_PIC_PATTERN - can be not supported - check Capability Manager sample
				res = encoder->SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED);
				AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_QUALITY_PRESET, AMF_VIDEO_ENCODER_QUALITY_PRESET_SPEED) failed");
			}

			res = encoder->SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, bitRateIn);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_TARGET_BITRATE, %" LPRId64 L") failed", bitRateIn);
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_FRAMERATE, %dx%d) failed", frameRateIn, 1);

#if defined(ENABLE_4K)
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE, AMF_VIDEO_ENCODER_PROFILE_HIGH) failed");

			res = encoder->SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_PROFILE_LEVEL, 51)");
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_B_PIC_PATTERN, 0)");
#endif
		}
		else
		{
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_USAGE, AMF_VIDEO_ENCODER_HEVC_USAGE_TRANSCODING)");

			if (bMaximumSpeed)
			{
				res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED);
				AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET, AMF_VIDEO_ENCODER_HEVC_QUALITY_PRESET_SPEED)");
			}

			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, bitRateIn);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TARGET_BITRATE, %" LPRId64 L") failed", bitRateIn);
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, ::AMFConstructRate(frameRateIn, 1));
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_FRAMERATE, %dx%d) failed", frameRateIn, 1);

			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, eDepth);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_COLOR_BIT_DEPTH, %d) failed", eDepth);


#if defined(ENABLE_4K)
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_TIER, AMF_VIDEO_ENCODER_HEVC_TIER_HIGH) failed");
			res = encoder->SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1);
			AMF_RETURN_IF_FAILED(res, L"SetProperty(AMF_VIDEO_ENCODER_HEVC_PROFILE_LEVEL, AMF_LEVEL_5_1) failed");
#endif
		}
		res = encoder->Init(formatIn, widthIn, heightIn);
		AMF_RETURN_IF_FAILED(res, L"encoder->Init() failed");


		PollingThread thread(context, encoder, amf_wstring(pCodec) == amf_wstring(AMFVideoEncoderVCE_AVC) ? fileNameOut_h264 : fileNameOut_h265);
		thread.Start();

		// encode some frames
		amf_int32 submitted = 0;
		while (/*submitted < frameCount*/true)
		{
			if (surfaceIn == NULL)
			{
				surfaceIn = NULL;
				res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn);
				AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

				//if (memoryTypeIn == amf::AMF_MEMORY_VULKAN)
				//{
				//	FillSurfaceVulkan(context, surfaceIn);
				//}
#ifdef _WIN32
				if (memoryTypeIn == amf::AMF_MEMORY_DX9)
				{
					//FillSurfaceDX9(context, surfaceIn);
				}
				else
				{
					//FillSurfaceDX11(context, surfaceIn);
					
					surfaceIn = NULL;
					res = context->AllocSurface(memoryTypeIn, formatIn, widthIn, heightIn, &surfaceIn);
					AMF_RETURN_IF_FAILED(res, L"AllocSurface() failed");

					ID3D11Device* deviceDX11 = (ID3D11Device*)context->GetDX11Device(); // no reference counting - do not Release()
					ID3D11Texture2D* surfaceDX11 = (ID3D11Texture2D*)surfaceIn->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()

					ID3D11DeviceContext* deviceContextDX11 = NULL;
					deviceDX11->GetImmediateContext(&deviceContextDX11);

					//ID3D11Texture2D* surfaceDX11Color1 = (ID3D11Texture2D*)pColor1->GetPlaneAt(0)->GetNative(); // no reference counting - do not Release()
					if (texture) {

						HANDLE tex_handle = NULL;
						IDXGIResource* pResource = nullptr;
						auto hr = texture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&pResource));
						if (!FAILED(hr))
						{
							hr = pResource->GetSharedHandle(&tex_handle);
							if (FAILED(hr)) {
								printf("GetSharedHandle Failed !!! \n");
								return 0;
							}

						}
						printf("texture handle ; %llu \n", tex_handle);
						ID3D11Texture2D* target_texture;
						if (FAILED(deviceDX11->OpenSharedResource(
							tex_handle, __uuidof(ID3D11Texture2D), (void**)&target_texture)))
						{
							printf("OpenSharedResource failed !\n");
							return 0;
						}

						printf("open shared resource success. \n");

						deviceContextDX11->CopyResource(surfaceDX11, target_texture);
					}
				}
#endif
			}
			// encode
			amf_pts start_time = amf_high_precision_clock();
			surfaceIn->SetProperty(START_TIME_PROPERTY, start_time);
			// 264
			//surfaceIn->SetProperty(AMF_VIDEO_ENCODER_INSERT_AUD, false);
			//surfaceIn->SetProperty(AMF_VIDEO_ENCODER_HEVC_INSERT_HEADER, true);
			//surfaceIn->SetProperty(AMF_VIDEO_ENCODER_HEVC_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_HEVC_PICTURE_TYPE_IDR);

			surfaceIn->SetProperty(AMF_VIDEO_ENCODER_INSERT_SPS, true);
			surfaceIn->SetProperty(AMF_VIDEO_ENCODER_INSERT_PPS, true);
			surfaceIn->SetProperty(AMF_VIDEO_ENCODER_FORCE_PICTURE_TYPE, AMF_VIDEO_ENCODER_PICTURE_TYPE_IDR);

			res = encoder->SubmitInput(surfaceIn);
			if (res == AMF_NEED_MORE_INPUT) // handle full queue
			{
				// do nothing
			}
			else if (res == AMF_INPUT_FULL || res == AMF_DECODER_NO_FREE_SURFACES)
			{
				amf_sleep(1); // input queue is full: wait, poll and submit again
			}
			else
			{
				AMF_RETURN_IF_FAILED(res, L"SubmitInput() failed");
				surfaceIn = NULL;
				submitted++;
			}
		}
		// drain encoder; input queue can be full
		while (true)
		{
			res = encoder->Drain();
			if (res != AMF_INPUT_FULL) // handle full queue
			{
				break;
			}
			amf_sleep(1); // input queue is full: wait and try again
		}
		thread.WaitForStop();

 

		// cleanup in this order
		surfaceIn = NULL;
		encoder->Terminate();
		encoder = NULL;
		context->Terminate();
		context = NULL; // context is the last

		g_AMFFactory.Terminate();
#endif
		return 0;
	}


	PollingThread::PollingThread(amf::AMFContext* context, amf::AMFComponent* encoder, const wchar_t* pFileName) : m_pContext(context), m_pEncoder(encoder)
	{
		std::wstring wStr(pFileName);
		std::string str(wStr.begin(), wStr.end());
		m_pFile.open(str, std::ios::binary);
	}
	PollingThread::~PollingThread()
	{
		if (m_pFile)
		{
			m_pFile.close();
		}
	}
	void PollingThread::Run()
	{
		RequestStop();

		amf_pts latency_time = 0;
		amf_pts write_duration = 0;
		amf_pts encode_duration = 0;
		amf_pts last_poll_time = 0;

		AMF_RESULT res = AMF_OK; // error checking can be added later
		while (true)
		{
			amf::AMFDataPtr data;
			res = m_pEncoder->QueryOutput(&data);
			if (res == AMF_EOF)
			{
				break; // Drain complete
			}
			if ((res != AMF_OK) && (res != AMF_REPEAT))
			{
				// trace possible error message
				break; // Drain complete
			}
			if (data != NULL)
			{
				amf_pts poll_time = amf_high_precision_clock();
				amf_pts start_time = 0;
				data->GetProperty(START_TIME_PROPERTY, &start_time);
				if (start_time < last_poll_time) // remove wait time if submission was faster then encode
				{
					start_time = last_poll_time;
				}
				last_poll_time = poll_time;

				encode_duration += poll_time - start_time;

				if (latency_time == 0)
				{
					latency_time = poll_time - start_time;
				}

				amf::AMFBufferPtr buffer(data); // query for buffer interface
				m_pFile.write(reinterpret_cast<char*>(buffer->GetNative()), buffer->GetSize());

				printf("buffer size : %d \n", buffer->GetSize());

				write_duration += amf_high_precision_clock() - poll_time;
			}
			else
			{
				amf_sleep(1);
			}
		}
		printf("latency           = %.4fms\nencode  per frame = %.4fms\nwrite per frame   = %.4fms\n",
			double(latency_time) / MILLISEC_TIME,
			double(encode_duration) / MILLISEC_TIME / frameCount,
			double(write_duration) / MILLISEC_TIME / frameCount);

		m_pEncoder = NULL;
		m_pContext = NULL;
	}

}

#endif