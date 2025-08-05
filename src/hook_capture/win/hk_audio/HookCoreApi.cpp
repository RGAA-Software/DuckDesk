#include "HookCoreApi.h"
#include "common/RLog.h"
#include "HookCommon.h"
#include "common/RFile.h"
#include "AudioShare.h"
#include "ipc/IPCMessage.h"
#include "ipc/InterCommClient.h"

#define DEBUG_AUDIO 0

namespace tc
{

#if DEBUG_AUDIO
	static std::ofstream audio_file("111.data", std::ios::binary);
	static std::ofstream audio_file2("222.data", std::ios::binary);
#endif

	HRESULT __stdcall Hook_GetBuffer(IAudioRenderClient* thiz, UINT32 NumFramesRequested, BYTE** ppData) {
		auto ret = origin_GetBuffer(thiz, NumFramesRequested, ppData);
		HookCoreApi::Instance()->buffer = (char*)*ppData;
		return ret;
	}
	
	HRESULT __stdcall Hook_ReleaseBuffer(IAudioRenderClient* thiz, UINT32 NumFramesWritten, DWORD dwFlags) {
		auto api_instance = HookCoreApi::Instance();
		if (api_instance->buffer) {
			int buffer_size = NumFramesWritten * api_instance->block_align;
			auto data = Data::Make(api_instance->buffer, buffer_size);
			api_instance->audio_share->PostAudioData(data);
#if DEBUG_AUDIO
			if (api_instance->format == SimpleAudioFormat::kPCM_S16) {
				audio_file.write(api_instance->buffer, buffer_size);
			} 
			else if (api_instance->format == SimpleAudioFormat::kPCM_F32) {
				auto data = SimpleAudioFormatConverter::CvtF32ToS16(api_instance->buffer, buffer_size);
				audio_file.write(data->CStr(), data->Size());
				audio_file2.write(api_instance->buffer, buffer_size);
			}
			else {
				LOG_ERROR("Not supported audio format !");
			}
#endif
			api_instance->buffer = nullptr;
		}

		return origin_ReleaseBuffer(thiz, NumFramesWritten, dwFlags);
	}

	HRESULT __stdcall Hook_GetMixFormat(IAudioClient* thiz, WAVEFORMATEX** ppDeviceFormat) {
		auto ret = origin_GetMixFormat(thiz, ppDeviceFormat);
		return ret;
	}

	HRESULT __stdcall Hook_Initialize(
		IAudioClient* thiz,
		AUDCLNT_SHAREMODE ShareMode,
		DWORD StreamFlags,
		REFERENCE_TIME hnsBufferDuration,
		REFERENCE_TIME hnsPeriodicity,
		const WAVEFORMATEX* pFormat,
		LPCGUID AudioSessionGuid) {

		auto api_instance = HookCoreApi::Instance();

		auto ret = origin_Initialize(thiz, ShareMode, StreamFlags, hnsBufferDuration, hnsPeriodicity, pFormat, AudioSessionGuid);
		auto format = pFormat;

		LOG_INFO("Hook_Initialize///");
		LOG_INFO("MixFormat : samples: %d, channels: %d, bitsPerSamples: %d, wFormatTag : 0x%x, cbSize : %d, nBlockAlign : %d",
			format->nSamplesPerSec, format->nChannels, format->wBitsPerSample, format->wFormatTag, format->cbSize, format->nBlockAlign);

		api_instance->samples = format->nSamplesPerSec;
		api_instance->channels = format->nChannels;
		api_instance->bits = format->wBitsPerSample;
		api_instance->block_align = format->nBlockAlign;

		switch (pFormat->wFormatTag) {
			case WAVE_FORMAT_PCM: {
				LOG_INFO("WAVE_FORMAT_PCM ----");
				api_instance->format = SimpleAudioFormat::kPCM_S16;
				break;
			}
			case WAVE_FORMAT_IEEE_FLOAT: {
				LOG_INFO("WAVE_FORMAT_IEEE_FLOAT ----");
				api_instance->format = SimpleAudioFormat::kPCM_F32;
				break;
			}
			case WAVE_FORMAT_EXTENSIBLE: {
				const WAVEFORMATEXTENSIBLE* extFmt = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(pFormat);
				if (extFmt->SubFormat == KSDATAFORMAT_SUBTYPE_IEEE_FLOAT) {
					//WASAPIHook::GetInstance()->SetPcmType(cloudapp::Frame_Type_kPcmF32LE);
					LOG_INFO("WAVEFORMATEXTENSIBLE /// /// samples : %d , channelMask : %d", extFmt->Samples, extFmt->dwChannelMask);
					api_instance->format = SimpleAudioFormat::kPCM_F32;
				}
				else if (extFmt->SubFormat == KSDATAFORMAT_SUBTYPE_PCM) {
					LOG_INFO("WAVEFORMATEXTENSIBLE PCM FORMAT ... samples : %d , channelMask : %d", extFmt->Samples, extFmt->dwChannelMask);
					api_instance->format = SimpleAudioFormat::kPCM_S16;
				}
				else {
					LOG_INFO("Not support audio format : %d", extFmt->SubFormat);
				}
				break;;
			}
		}

		if (api_instance->format != SimpleAudioFormat::kPCM_S16 
			&& api_instance->format != SimpleAudioFormat::kPCM_F32) {
			LOG_INFO("Not support audio format : %d", pFormat->wFormatTag);
		}

		api_instance->audio_share->SetAudioFormat(api_instance->format);
	
		auto audio_hooked_message = std::make_shared<IPCAudioHookedMessage>();
		audio_hooked_message->type = IPCMessageType::kSharedAudioHooked;
		audio_hooked_message->sender = IPCMessageSender::kSenderClient;
		audio_hooked_message->samples = pFormat->nSamplesPerSec;
		audio_hooked_message->channels = pFormat->nChannels;
		audio_hooked_message->bits = 16;
		auto data = ConvertIPCMessageToData<IPCAudioHookedMessage>(audio_hooked_message);
		InterCommClient::Instance()->SendBack(data);

		return ret;
	}


	HookCoreApi::HookCoreApi() {
	}

	HookCoreApi::~HookCoreApi() {
		api_GetBuffer.Unhook();
	}

	bool HookCoreApi::Init() {
		audio_share = std::make_shared<AudioShare>();
		int ret = -1;
		HRESULT hr;
		IMMDeviceEnumerator* deviceEnumerator = NULL;
		IMMDevice* device = NULL;
		IAudioClient* audioClient = NULL;
		IAudioRenderClient* renderClient = NULL;
		WAVEFORMATEX* pwfx = NULL;


		hr = CoInitialize(NULL);

#define	RET_ON_ERROR(hr, prefix) if(hr!=S_OK) { LOG_INFO("[core-audio] %s failed (%08x).\n", prefix, hr); goto hook_ca_quit; }
		hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), NULL, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), (void**)&deviceEnumerator);
		RET_ON_ERROR(hr, "CoCreateInstance");

		hr = deviceEnumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
		RET_ON_ERROR(hr, "GetDefaultAudioEndpoint");

		hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, NULL, (void**)&audioClient);
		RET_ON_ERROR(hr, "Activate");

		hr = audioClient->GetMixFormat(&pwfx);
		RET_ON_ERROR(hr, "GetMixFormat");

		hr = audioClient->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000/*REFTIME_PER_SEC*/, 0, pwfx, NULL);
		RET_ON_ERROR(hr, "Initialize");

		hr = audioClient->GetService(__uuidof(IAudioRenderClient), (void**)&renderClient);
		RET_ON_ERROR(hr, "GetService[IAudioRenderClient]");
#undef	RET_ON_ERROR

		NTSTATUS status;
		origin_GetMixFormat = (FUNC_GetMixFormat)((comobj_t*)audioClient)->vtbl->func[8];
		status = tc::HookAllThread(api_GetMixFormat, origin_GetMixFormat, Hook_GetMixFormat);
		if (!NT_SUCCESS(status)) {
			LOG_ERROR("HOOK ERROR : GetMixFormat");
			return false;
		}

		origin_GetBuffer = (FUNC_GetBuffer)((comobj_t*)renderClient)->vtbl->func[3];
		status = tc::HookAllThread(api_GetBuffer, origin_GetBuffer, Hook_GetBuffer);
		if (!NT_SUCCESS(status)) {
			LOG_ERROR("HOOK ERROR : GetBuffer");
			return false;
		}

		origin_ReleaseBuffer = (FUNC_ReleaseBuffer)((comobj_t*)renderClient)->vtbl->func[4];
		status = tc::HookAllThread(api_ReleaseBuffer, origin_ReleaseBuffer, Hook_ReleaseBuffer);
		if (!NT_SUCCESS(status)) {
			LOG_ERROR("HOOK ERROR : ReleaseBuffer");
			return false;
		}

		//
		origin_Initialize = (FUNC_Initialize)((comobj_t*)audioClient)->vtbl->func[3];
		status = tc::HookAllThread(api_Initialize, origin_Initialize, Hook_Initialize);
		if (!NT_SUCCESS(status)) {
			LOG_ERROR("HOOK ERROR : Initialize");
			return false;
		}

		ret = 0;

		//LOG_INFO("Hook core audio done .\n");
hook_ca_quit:
		if (renderClient) { 
			renderClient->Release();	
			renderClient = NULL; 
		}

		if (pwfx) { 
			CoTaskMemFree(pwfx);		
			pwfx = NULL; 
		}

		if (audioClient) { 
			audioClient->Release();	
			audioClient = NULL; 
		}

		if (device) { 
			device->Release();		
			device = NULL; 
		}

		if (deviceEnumerator) { 
			deviceEnumerator->Release();	
			deviceEnumerator = NULL; 
		}
		return ret; 
	}
}