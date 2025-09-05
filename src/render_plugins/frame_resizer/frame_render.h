#pragma once

#include "resize_common_types.h"
#include <string>
#include <memory>
#include "tc_common_new/win32/d3d11_wrapper.h"

namespace tc
{

    class FrameRender {
    public:
        static std::shared_ptr<FrameRender> Make(ID3D11Device *device, ID3D11DeviceContext *context);

        FrameRender(ID3D11Device *device, ID3D11DeviceContext *context);
        ~FrameRender();

        HRESULT Prepare(SIZE targetSize, SIZE originSize, int format);
        HRESULT Draw();

        ComPtr<ID3D11Device> GetD3D11Device() {
            return m_Device;
        }

        ComPtr<ID3D11DeviceContext> GetD3D11DeviceContext() {
            return m_DeviceContext;
        }

        ComPtr<ID3D11Texture2D> GetFinalTexture() {
            return m_FinalTexture;
        }

        ComPtr<ID3D11Texture2D> GetSrcTexture() {
            return m_SrcTexture;
        }

        int GetTargetWidth() const {
            return target_size_.cx;
        }

        int GetTargetHeight() const {
            return target_size_.cy;
        }

    private:
        HRESULT InitializeDesc(_In_ SIZE size, _Out_ D3D11_TEXTURE2D_DESC *pTargetDesc, int format);
        HRESULT MakeRTV();
        void SetViewPort(SIZE size);
        HRESULT InitShaders();
        void CleanRefs();

    private:
        ComPtr<ID3D11Device> m_Device = nullptr;
        ComPtr<ID3D11DeviceContext> m_DeviceContext = nullptr;
        ComPtr<ID3D11SamplerState> m_SamplerLinear = nullptr;
        ComPtr<ID3D11BlendState> m_BlendState = nullptr;
        ComPtr<ID3D11VertexShader> m_VertexShader = nullptr;
        ComPtr<ID3D11PixelShader> m_PixelShader = nullptr;
        ComPtr<ID3D11InputLayout> m_InputLayout = nullptr;
        ComPtr<ID3D11Texture2D> m_TargetTexture = nullptr;
        ComPtr<ID3D11RenderTargetView> m_RTV = nullptr;
        ComPtr<ID3D11Texture2D> m_SrcTexture = nullptr;
        ComPtr<ID3D11ShaderResourceView> m_SrcSrv = nullptr;
        ComPtr<ID3D11Texture2D> m_FinalTexture = nullptr;
        ComPtr<ID3D11Buffer> VertexBuffer = nullptr;
        SIZE target_size_{};
    };

}