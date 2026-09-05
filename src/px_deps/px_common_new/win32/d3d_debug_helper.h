//
// Created by RGAA  on 2024/1/6.
//

#ifndef TC_APPLICATION_D3D_DEBUG_HELPER_H
#define TC_APPLICATION_D3D_DEBUG_HELPER_H
#include <string>
#include <atlbase.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace px
{

    void PrintD3DTexture2DDesc(const std::string& name, const D3D11_TEXTURE2D_DESC& desc);
    void PrintD3DTexture2DDesc(const std::string& name, const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool DebugOutDDS(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& resource, const std::string& name);
    bool D3D11Texture2DLockMutex(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool D3D11Texture2DReleaseMutex(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture);
    bool CopyID3D11Texture2D(const Microsoft::WRL::ComPtr<ID3D11Texture2D>& shared_texture);

}
#endif //TC_APPLICATION_D3D_DEBUG_HELPER_H



