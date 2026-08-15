#pragma once

#include <d3d11.h>
#include <memory>
#include <optional>
#include "shared_texture.h"

HRESULT CaptureTexture(ID3D11Device* device,
                       ID3D11DeviceContext* context,
                       ID3D11Resource* source,
                       D3D11_TEXTURE2D_DESC& desc,
                       CComPtr<ID3D11Texture2D>& staging,
                       std::shared_ptr<tc::SharedTexture> shared_texture = nullptr);

namespace tc {
    //获取显卡唯一标识 ，后续需要再测试下D3D12的游戏
    std::optional<int64_t> GetAdapterUid(CComPtr<ID3D11Device> d3d11_device);
}


