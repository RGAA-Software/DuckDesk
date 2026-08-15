//
// Created by RGAA on 1/05/2025.
//

#ifndef GAMMARAY_D3D11_WRAPPER_H
#define GAMMARAY_D3D11_WRAPPER_H
#ifdef WIN32

#include <memory>
#include <d3d11.h>
#include <wrl/client.h>

using namespace Microsoft::WRL;

namespace px
{

    class D3D11DeviceWrapper {
    public:
        // 必须用 Reset() 而不是裸 ->Release():裸 Release 只减引用计数,ComPtr
        // 成员仍持有(可能悬空的)指针,之后 wrapper 析构/Reset 会再 Release 一次,
        // 造成重复释放——其他持有者(如 VideoFrameCarrier)的 ComPtr 随即悬空,
        // device removed → HandleD3DDeviceFailure → 重建 carrier 时在 Exit 里崩。
        void Release() {
            if (d3d11_device_context_) {
                d3d11_device_context_->ClearState();
                d3d11_device_context_->Flush();
            }
            d3d11_device_context_.Reset();
            d3d11_device_.Reset();
        }

        bool IsValid() const {
            return d3d11_device_ && d3d11_device_context_;
        }

    public:
        uint64_t adapter_uid_ = 0;
        ComPtr<ID3D11Device> d3d11_device_ = nullptr;
        ComPtr<ID3D11DeviceContext> d3d11_device_context_ = nullptr;
    };

}
#endif
#endif //GAMMARAY_D3D11_WRAPPER_H
