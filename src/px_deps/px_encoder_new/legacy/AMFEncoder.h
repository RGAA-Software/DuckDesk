#pragma once

#if 0 // test

#include <memory>
#include <stdio.h>
#ifdef _WIN32

#include <tchar.h>
#include <d3d9.h>
#include <d3d11.h>
#endif
#include "amf/common/AMFFactory.h"
#include "amf/include/components/VideoEncoderVCE.h"
#include "amf/include/components/VideoEncoderHEVC.h"
#include "amf/include/components/ColorSpace.h"
#include "amf/common/Thread.h"
#include "amf/common/AMFSTL.h"
#include "amf/common/TraceAdapter.h"
#include <fstream>

//#define ENABLE_4K
//#define ENABLE_EFC // color conversion inside encoder component. Will use GFX or HW if available
//#define ENABLE_10_BIT

#if defined(ENABLE_10_BIT)
static const wchar_t* pCodec = AMFVideoEncoder_HEVC;
static AMF_COLOR_BIT_DEPTH_ENUM eDepth = AMF_COLOR_BIT_DEPTH_10;
#else
static const wchar_t *pCodec = AMFVideoEncoderVCE_AVC;
//static const wchar_t* pCodec = AMFVideoEncoder_HEVC;
static AMF_COLOR_BIT_DEPTH_ENUM eDepth = AMF_COLOR_BIT_DEPTH_8;
#endif

#ifdef _WIN32
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_DX11;
#else
static amf::AMF_MEMORY_TYPE memoryTypeIn = amf::AMF_MEMORY_VULKAN;
#endif

#if defined ENABLE_EFC
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_RGBA;
//static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_BGRA;
//static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_R10G10B10A2;
//static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_RGBA_F16;
#else
//static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_NV12;
static amf::AMF_SURFACE_FORMAT formatIn = amf::AMF_SURFACE_RGBA;
#endif

#if defined(ENABLE_4K)
static amf_int32 widthIn = 1920 * 2;
static amf_int32 heightIn = 1080 * 2;

#else
static amf_int32 widthIn = 1920;
static amf_int32 heightIn = 1080;
#endif
static amf_int32 frameRateIn = 60;
static amf_int64 bitRateIn = 5000000L; // in bits, 5MBit
static amf_int32 rectSize = 50;
static amf_int32 frameCount = 500;
static bool bMaximumSpeed = true;

#define START_TIME_PROPERTY L"StartTimeProperty" // custom property ID to store submission time in a frame - all custom properties are copied from input to output

static const wchar_t* fileNameOut_h264 = L"./output.h264";
static const wchar_t* fileNameOut_h265 = L"./output.h265";

#define MILLISEC_TIME     10000

namespace tc
{

	class PollingThread : public amf::AMFThread
	{
	protected:
		amf::AMFContextPtr      m_pContext;
		amf::AMFComponentPtr    m_pEncoder;
		std::ofstream           m_pFile;
	public:
		PollingThread(amf::AMFContext* context, amf::AMFComponent* encoder, const wchar_t* pFileName);
		~PollingThread();
		virtual void Run();
	};

	class AMFEncoder {
	public:
		
		static std::shared_ptr<AMFEncoder> Make(int width, int height);

		AMFEncoder(int width, int height);
		~AMFEncoder();

		int Start();

		void SetTexture(ID3D11Texture2D* pTexture);

	private:
		int width = 0;
		int height = 0;
		ID3D11Texture2D* texture = nullptr;
	};
}
#endif