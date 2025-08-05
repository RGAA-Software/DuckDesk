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

	public:

		CComPtr<ID3D11Texture2D> texture_ = nullptr;
		D3D11_TEXTURE2D_DESC curr_desc_;
		
	};

}