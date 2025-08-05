#pragma once

#include <mmdeviceapi.h>
#include <audioclient.h>

#include "easyhook/easyhook.h"
#include "hk_utils/hook_api.hpp"
#include "SimpleAudioFormatConverter.h"

#define DllImport   __declspec( dllimport )
#define DllExport   __declspec( dllexport )

namespace tc
{

	typedef HRESULT(STDMETHODCALLTYPE* FUNC_GetBuffer)(
		IAudioRenderClient* thiz,
		UINT32 NumFramesRequested,
		BYTE** ppData);

	typedef HRESULT(STDMETHODCALLTYPE* FUNC_ReleaseBuffer)(
		IAudioRenderClient* thiz,
		UINT32 NumFramesWritten,
		DWORD dwFlags);

	typedef HRESULT(STDMETHODCALLTYPE* FUNC_GetMixFormat)(
		IAudioClient* thiz,
		WAVEFORMATEX** ppDeviceFormat);

	typedef HRESULT(STDMETHODCALLTYPE* FUNC_Initialize)(
		IAudioClient* thiz,
		AUDCLNT_SHAREMODE ShareMode,
		DWORD StreamFlags,
		REFERENCE_TIME hnsBufferDuration,
		REFERENCE_TIME hnsPeriodicity,
		const WAVEFORMATEX* pFormat,
		LPCGUID AudioSessionGuid);
	
	class AudioShare;

	class HookCoreApi {
	public:

		static HookCoreApi* Instance() {
			static HookCoreApi api;
			return &api;
		}

		HookCoreApi();
		~HookCoreApi();

		bool Init();

	public:
		SimpleAudioFormat format;
		int samples = 0;
		int channels = 0;
		int bits = 0;
		int block_align = 0;
		char* buffer = nullptr;

		std::shared_ptr<AudioShare> audio_share = nullptr;

	private:
		
		tc::HookApi api_GetBuffer;
		tc::HookApi api_ReleaseBuffer;
		tc::HookApi api_GetMixFormat;
		tc::HookApi api_Initialize;

	};

	static FUNC_GetBuffer		origin_GetBuffer = nullptr;
	static FUNC_ReleaseBuffer	origin_ReleaseBuffer = nullptr;
	static FUNC_GetMixFormat	origin_GetMixFormat = nullptr;
	static FUNC_Initialize		origin_Initialize = nullptr;

}