#pragma once

#include <dxgi.h>
#include <d3d11.h>
#include <windows.h>
#include <atlbase.h>
#include <memory>

namespace tc
{

	class SharedTexture {
	public:

		SharedTexture() = default;
		~SharedTexture() = default;

		bool CopyCapturedTexture(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* src);
		uint64_t GetSharedHandle();

	private:
        bool LockMutex();
        bool ReleaseMutex();
        // 10bit(R10G10B10A2 等) swapchain 格式直接共享给消费端会导致 NVENC 失败甚至 TDR,
        // 在生产端统一转成 B8G8R8A8;且 11on12 上 R10G10B10A2+KEYEDMUTEX 创建会 E_INVALIDARG。
        bool EnsureConvertShaders(ID3D11Device* device);
        bool BlitConvertToBgra(ID3D11Device* device, ID3D11DeviceContext* context, ID3D11Texture2D* src);
        static bool IsDirectShareableFormat(DXGI_FORMAT format);

	public:

		CComPtr<ID3D11Texture2D> texture_ = nullptr;
		D3D11_TEXTURE2D_DESC curr_desc_;

	private:
		CComPtr<ID3D11VertexShader> conv_vs_ = nullptr;
		CComPtr<ID3D11PixelShader> conv_ps_ = nullptr;
		bool conv_shader_failed_ = false;

	};

}
