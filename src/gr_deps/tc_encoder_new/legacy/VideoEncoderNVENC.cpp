#if 1
#include "VideoEncoderNVENC.h"
#include "NvCodecUtils.h"
#include "NVENCoderClioptions.h"

#include <wincodec.h>
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <format>
#include "tc_common_new/log.h"
#include "tc_common_new/time_util.h"
#include "tc_common_new/data.h"
#include "tc_common_new/win32/d3d_debug_helper.h"
#include "D3DTextureDebug.h"

using namespace tc;

VideoEncoderNVENC::VideoEncoderNVENC(std::shared_ptr<CD3DRender> pD3DRender, EncodeDataCallback data_cbk, bool useNV12, int width, int height)
	: LegacyVideoEncoder(EVideoCodecType::kH264), m_pD3DRender(pD3DRender)
	, m_nFrame(0)
	, m_useNV12(useNV12)
{
	m_width = width;
	m_height = height;
	m_Listener = data_cbk;
}

VideoEncoderNVENC::~VideoEncoderNVENC()
{
	
}

bool VideoEncoderNVENC::Initialize()
{
	
	return true;
}

static NV_ENC_BUFFER_FORMAT DxgiFormatToNv(DXGI_FORMAT dxgiFormat)
{
	switch (dxgiFormat)
	{
	case DXGI_FORMAT_NV12:
		return NV_ENC_BUFFER_FORMAT_NV12;
	case DXGI_FORMAT_B8G8R8A8_UNORM:
	case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
		return NV_ENC_BUFFER_FORMAT_ABGR;

	case DXGI_FORMAT_R8G8B8A8_UNORM:
	case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
		return NV_ENC_BUFFER_FORMAT_ARGB;

	case DXGI_FORMAT_R10G10B10A2_UNORM:
		return NV_ENC_BUFFER_FORMAT_ARGB10;

	default:
		return NV_ENC_BUFFER_FORMAT_UNDEFINED;
	}
}

bool VideoEncoderNVENC::InitEncoder(ID3D11Texture2D* texture) {
	Shutdown();

	//auto options = "-preset ll_hp -rc cbr -fps 60 -bitrate 5M -maxbitrate 6M"; // ll_hq cbr vbr
	//NvEncoderInitParam EncodeCLIOptions(options);

	D3D11_TEXTURE2D_DESC desc;
	texture->GetDesc(&desc);

	NV_ENC_BUFFER_FORMAT format = DxgiFormatToNv(desc.Format);
	m_width = desc.Width;
	m_height = desc.Height;
	LOGI("width : %d, height : %d, dxgi format : %d, nv format : 0x%x", m_width, m_height, desc.Format, format);

	try {
		m_NvNecoder = std::make_shared<NvEncoderD3D11>(m_pD3DRender->GetDevice(), m_width, m_height, format, 0);
	}
	catch (NVENCException e) {
		OnInitFailed();
		LOGI("Exception , code : %d,  %s", e.getErrorCode(), e.getErrorString());
		return false;
	}

	NV_ENC_INITIALIZE_PARAMS initializeParams = { NV_ENC_INITIALIZE_PARAMS_VER };
	NV_ENC_CONFIG encodeConfig = { NV_ENC_CONFIG_VER };

	initializeParams.encodeConfig = &encodeConfig;

	GUID EncoderGUID = NV_ENC_CODEC_H264_GUID;// : NV_ENC_CODEC_HEVC_GUID;
	m_NvNecoder->CreateDefaultEncoderParams(&initializeParams, EncoderGUID, NV_ENC_PRESET_LOW_LATENCY_HQ_GUID/*EncodeCLIOptions.GetPresetGUID()*/);

	bool supportsIntraRefresh = m_NvNecoder->GetCapabilityValue(EncoderGUID, NV_ENC_CAPS_SUPPORT_INTRA_REFRESH);

	//

	// test
	int refreshRate = 60;
	int gop = 60;
	// use 0 for default
	int maxNumRefFrames = 0;

	//if (m_codec == Running::EncoderType::kEncodeH264) {
		initializeParams.encodeConfig->encodeCodecConfig.h264Config.repeatSPSPPS = 1;
	//}
//	else if (m_codec == Running::kEncodeH265) {
//		initializeParams.encodeConfig->profileGUID = NV_ENC_HEVC_PROFILE_MAIN_GUID;
//		auto& hevc_config = initializeParams.encodeConfig->encodeCodecConfig.hevcConfig;
//		hevc_config.repeatSPSPPS = 1;
//		if (supportsIntraRefresh) {
//			hevc_config.enableIntraRefresh = 1;
//			// Do intra refresh every 10sec.
//			hevc_config.intraRefreshPeriod = refreshRate * 10;
//			hevc_config.intraRefreshCnt = refreshRate;
//		}
//		hevc_config.maxNumRefFramesInDPB = maxNumRefFrames;
//		hevc_config.idrPeriod = gop;
//	}

	//EncodeCLIOptions.SetInitParams(&initializeParams, format);
	//initializeParams.encodeConfig->rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR_HQ;
	//std::cout << "initialParams // : " << initializeParams.encodeConfig->rcParams.rateControlMode << std::endl;
	//std::string parameterDesc = EncodeCLIOptions.FullParamToString(&initializeParams);
	//LOGI("NvEnc Encoder Parameters:\n%hs", parameterDesc.c_str());

	try {
		m_NvNecoder->CreateEncoder(&initializeParams);
	}
	catch (NVENCException e) {
		OnInitFailed();
		LOGI("CreateEncoder Exception : %s", e.what());
		return false;
	}

	if (false && !fpOut.is_open()) {
		fpOut = std::ofstream("nnnnn.h265", std::ios::out | std::ios::binary | std::ios::beg);
		if (!fpOut)
		{
			LOGI("unable to open output file");
		}
	}

	OnInitSuccess();
	LOGI("CNvEncoder is successfully initialized.");
}

void VideoEncoderNVENC::Shutdown()
{
	if (!m_NvNecoder) {
		return;
	}
	std::vector<std::vector<uint8_t>> vPacket;
	m_NvNecoder->EndEncode(vPacket);
	for (std::vector<uint8_t> &packet : vPacket)
	{
		if (fpOut) {
			fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
		}
		//m_Listener->SendVideo(packet.data(), (int)packet.size(), 0);
	}

	m_NvNecoder->DestroyEncoder();
	m_NvNecoder.reset();

	LOGI("CNvEncoder::Shutdown");

	if (fpOut) {
		fpOut.close();
	}
}

std::chrono::high_resolution_clock::time_point beg = std::chrono::high_resolution_clock::now();
uint64_t count = 0;

void VideoEncoderNVENC::Transmit(uint64_t handle, uint64_t frame_index) {
	LegacyVideoEncoder::Transmit(handle, frame_index);
	EncodeTextureHandle(handle, frame_index);
}

void VideoEncoderNVENC::Transmit(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D *pTexture, uint64_t frame_index, bool insertIDR)
{
	LegacyVideoEncoder::Transmit(device, context, pTexture, frame_index, insertIDR);
	HANDLE tex_handle = NULL;
	CComPtr<IDXGIResource> pResource = nullptr;
	auto hr = pTexture->QueryInterface(__uuidof(IDXGIResource), reinterpret_cast<void**>(&pResource));
	if (!FAILED(hr)) {
		hr = pResource->GetSharedHandle(&tex_handle);
		if (FAILED(hr)) {
			LOGI("GetSharedHandle Failed !!!");
			return;
		}
	}
	if (!tex_handle) {
		return;
	}

	EncodeTextureHandle((uint64_t)tex_handle, frame_index);

}

void Delay(int time)
{
	clock_t now = clock();
	while (clock() - now < time);
}

void VideoEncoderNVENC::EncodeTextureHandle(uint64_t handle, uint64_t frame_index) {
    Microsoft::WRL::ComPtr<ID3D11Texture2D> target_texture = NULL;
	if (FAILED(m_pD3DRender->GetDevice()->OpenSharedResource((HANDLE)handle, __uuidof(ID3D11Texture2D), (void**)&target_texture))) {
		LOGE("OpenSharedResource failed !");
		return;
	}

	if (!m_NvNecoder) {
		if (InitEncoder(target_texture.Get())) {
			LOGI("Init NVENC encoder success.");
		}
		else {
			LOGE("Init NVENC encoder failed !");
			return;
		}
	}

	std::vector<std::vector<uint8_t>> vPacket;
	const NvEncInputFrame* encoderInputFrame = m_NvNecoder->GetNextInputFrame();
	D3D11_TEXTURE2D_DESC desc;

    if (!D3D11Texture2DLockMutex(target_texture)) {
        LOGE("D3D11Texture2DLockMutex error");
        return;
    }
    std::shared_ptr<void> auto_realse_texture2D_mutex((void *) nullptr, [=, this](void *temp) {
        D3D11Texture2DReleaseMutex(target_texture);
    });

	auto pInputTexture = reinterpret_cast<ID3D11Texture2D*>(encoderInputFrame->inputPtr);
	m_pD3DRender->GetContext()->CopyResource(pInputTexture, target_texture.Get());

	pInputTexture->GetDesc(&desc);

	//LOGI("-->Target<--");
	//D3DTextureDebug::PrintTextureDesc(pInputTexture);
	//LOGI("-->Source<--");
	//D3DTextureDebug::PrintTextureDesc(target_texture);
	//D3DTextureDebug::SaveAsDDS(m_pD3DRender->GetContext(), pInputTexture, L"encoder_texture.dds");
	//D3DTextureDebug::SaveAsDDS(m_pD3DRender->GetContext(), target_texture, L"source_texture.dds");

	auto end = std::chrono::high_resolution_clock::now();
	auto diff = std::chrono::duration<double, std::milli>(end - beg).count();
	beg = end;

#if 0
	LOGI("texture size : %d x %d ", desc.Width, desc.Height);
	if (pInputTexture) {

		static int count = 0;
		std::string name = "test_" + std::to_string(count++) + "";
		if (pInputTexture) {
			LOGI("Save to : %s ", name.c_str());
			//SaveTextureToBmp(name.c_str(), pInputTexture);
			bool success = DebugOutDDS(m_pD3DRender->GetDevice(), m_pD3DRender->GetContext(), pInputTexture, name);
			LOGI("save success ?  %d", success);
		}

		//return;
	}

#endif


	NV_ENC_PIC_PARAMS picParams = {};
	if (!insert_idr) {
		if (frame_index % gop == 0) {
			insert_idr = true;
		}
	}
	bool saved_is_key_frame = insert_idr;
	if (insert_idr) {
		LOGI("Inserting IDR frame.");
		picParams.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR;
		insert_idr = false;
	}

	auto en_beg = std::chrono::high_resolution_clock::now();
	m_NvNecoder->EncodeFrame(vPacket, &picParams);
	//Delay(16);
	auto en_end = std::chrono::high_resolution_clock::now();
	int duration = (int)std::chrono::duration<double, std::milli>(en_end - en_beg).count();
	//LOGI("Encode used time : %d, frame index : %d", duration, (int)frame_index);
	//std::cout << "Encode used time :" << duration << std::endl;
	//LOGI("vPacket size : %d", vPacket.size());
	//LOGI("Tracking info delay: %lld us FrameIndex=%llu", GetTimestampUs() - m_Listener->clientToServerTime(clientTime), frameIndex);
	//LOGI("Encoding delay: %lld us FrameIndex=%llu", GetTimestampUs() - presentationTime, frameIndex);

	m_nFrame += (int)vPacket.size();
	for (std::vector<uint8_t>& packet : vPacket) {
		//if (fpOut) {
		//	std::cout << "data size : " << packet.size() << std::endl;
		//	fpOut.write(reinterpret_cast<char*>(packet.data()), packet.size());
		//}
		if (m_Listener) {
			auto timestamp = TimeUtil::GetCurrentTimestamp();
			auto data = Data::Make((char*)packet.data(), packet.size());
			m_Listener(data, frame_index, saved_is_key_frame);
		}
	}
}

#endif