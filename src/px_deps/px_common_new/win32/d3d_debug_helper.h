//
// Created by RGAA  on 2024/1/6.
//

#ifndef TC_APPLICATION_D3D_DEBUG_HELPER_H
#define TC_APPLICATION_D3D_DEBUG_HELPER_H
#include <string>
#include <atlbase.h>
#include <d3d11.h>
#include <wrl/client.h>

namespace tc
{

    void PrintD3DTexture2DDesc(const std::string& name, const D3D11_TEXTURE2D_DESC& desc);
    void PrintD3DTexture2DDesc(const std::string& name, ID3D11Texture2D* tex);
    bool DebugOutDDS(ID3D11Texture2D *pResource, const std::string &name);
    bool D3D11Texture2DLockMutex(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d);
    bool D3D11Texture2DReleaseMutex(Microsoft::WRL::ComPtr<ID3D11Texture2D> texture2d);
    bool CopyID3D11Texture2D(Microsoft::WRL::ComPtr<ID3D11Texture2D> shared_texture2d);

}
#endif //TC_APPLICATION_D3D_DEBUG_HELPER_H



