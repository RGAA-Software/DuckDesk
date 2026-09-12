#include "d3d11_video_presenter.h"

#include "px_client_sdk/gl/raw_image.h"
#include "px_client_sdk/platform/windows/windows_video_frame.h"
#include "px_common/log.h"
#include "px_common/win32/d3d11_wrapper.h"

#include <d3d11.h>
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <expected>
#include <string_view>
#include <utility>
#include <vector>

namespace px::desktop {
namespace {

using Microsoft::WRL::ComPtr;

constexpr std::string_view kVertexShader{R"(
struct VsOut { float4 position : SV_POSITION; float2 uv : TEXCOORD0; };
VsOut main(uint id : SV_VertexID) {
    VsOut output;
    float2 uv = float2((id << 1) & 2, id & 2);
    output.position = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    output.uv = uv;
    return output;
}
)"};

constexpr std::string_view kNv12PixelShader{R"(
cbuffer FrameCoordinates : register(b0) { float2 uvScale; float2 padding; };
Texture2D<float> luminance : register(t0);
Texture2D<float2> chrominance : register(t1);
SamplerState linearSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    uv *= uvScale;
    float y = 1.16438356 * (luminance.Sample(linearSampler, uv) - 0.06274510);
    float2 c = chrominance.Sample(linearSampler, uv) - float2(0.5, 0.5);
    return float4(y + 1.79274107 * c.y, y - 0.21324861 * c.x - 0.53290933 * c.y, y + 2.11240179 * c.x, 1.0);
}
)"};

constexpr std::string_view kPlanarPixelShader{R"(
cbuffer FrameCoordinates : register(b0) { float2 uvScale; float2 padding; };
Texture2D<float> luminance : register(t0);
Texture2D<float> chromaU : register(t1);
Texture2D<float> chromaV : register(t2);
SamplerState linearSampler : register(s0);
float4 main(float4 position : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET {
    uv *= uvScale;
    float y = 1.16438356 * (luminance.Sample(linearSampler, uv) - 0.06274510);
    float u = chromaU.Sample(linearSampler, uv) - 0.5;
    float v = chromaV.Sample(linearSampler, uv) - 0.5;
    return float4(y + 1.79274107 * v, y - 0.21324861 * u - 0.53290933 * v, y + 2.11240179 * u, 1.0);
}
)"};

std::expected<ComPtr<ID3DBlob>, std::string> CompileShader(const std::string_view source, const std::string_view profile) {
    ComPtr<ID3DBlob> shader{};
    ComPtr<ID3DBlob> errors{};
    constexpr UINT flags{D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3};
    const HRESULT result{D3DCompile(source.data(), source.size(), nullptr, nullptr, nullptr, "main", profile.data(), flags, 0,
                                    shader.ReleaseAndGetAddressOf(), errors.ReleaseAndGetAddressOf())};
    if (SUCCEEDED(result))
        return shader;
    const auto message = errors ? std::string{static_cast<const char*>(errors->GetBufferPointer()), errors->GetBufferSize()} : "unknown error";
    return std::unexpected{message};
}

bool UploadPlane(ID3D11DeviceContext& context, ID3D11Texture2D& texture, const std::span<const char> source, const int stride) {
    if (source.empty() || stride <= 0)
        return false;
    context.UpdateSubresource(&texture, 0, nullptr, source.data(), static_cast<UINT>(stride), 0);
    return true;
}

} // namespace

struct D3d11VideoPresenter::Impl final {
    explicit Impl(std::shared_ptr<D3D11DeviceWrapper> deviceValue) : device{std::move(deviceValue)} {}

    std::shared_ptr<D3D11DeviceWrapper> device{};
    ComPtr<ID3D11VertexShader> vertexShader{};
    ComPtr<ID3D11PixelShader> nv12PixelShader{};
    ComPtr<ID3D11PixelShader> planarPixelShader{};
    ComPtr<ID3D11SamplerState> sampler{};
    ComPtr<ID3D11Buffer> frameCoordinatesBuffer{};
    ComPtr<ID3D11Texture2D> outputTexture{};
    ComPtr<ID3D11RenderTargetView> outputTarget{};
    ComPtr<ID3D11ShaderResourceView> outputView{};
    ComPtr<ID3D11Texture2D> nv12Texture{};
    std::array<ComPtr<ID3D11Texture2D>, 3> planarTextures{};
    std::array<ComPtr<ID3D11ShaderResourceView>, 3> planeViews{};
    RawImageFormat format{kRawImageRGB};
    int width{};
    int height{};
    int inputWidth{};
    int inputHeight{};

    bool CreateOutput(const int targetWidth, const int targetHeight) {
        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(targetWidth);
        description.Height = static_cast<UINT>(targetHeight);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        return SUCCEEDED(device->d3d11_device_->CreateTexture2D(&description, nullptr, outputTexture.ReleaseAndGetAddressOf())) &&
               SUCCEEDED(device->d3d11_device_->CreateRenderTargetView(outputTexture.Get(), nullptr, outputTarget.ReleaseAndGetAddressOf())) &&
               SUCCEEDED(device->d3d11_device_->CreateShaderResourceView(outputTexture.Get(), nullptr, outputView.ReleaseAndGetAddressOf()));
    }

    bool CreateNv12Input(const int textureWidth, const int textureHeight) {
        nv12Texture.Reset();
        planeViews[0].Reset();
        planeViews[1].Reset();
        D3D11_TEXTURE2D_DESC description{};
        description.Width = static_cast<UINT>(textureWidth);
        description.Height = static_cast<UINT>(textureHeight);
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_NV12;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->d3d11_device_->CreateTexture2D(&description, nullptr, nv12Texture.ReleaseAndGetAddressOf())))
            return false;
        D3D11_SHADER_RESOURCE_VIEW_DESC view{};
        view.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        view.Texture2D.MipLevels = 1;
        view.Format = DXGI_FORMAT_R8_UNORM;
        if (FAILED(device->d3d11_device_->CreateShaderResourceView(nv12Texture.Get(), &view, planeViews[0].ReleaseAndGetAddressOf())))
            return false;
        view.Format = DXGI_FORMAT_R8G8_UNORM;
        if (FAILED(device->d3d11_device_->CreateShaderResourceView(nv12Texture.Get(), &view, planeViews[1].ReleaseAndGetAddressOf())))
            return false;
        inputWidth = textureWidth;
        inputHeight = textureHeight;
        return true;
    }

    bool CreatePlanarInput() {
        const bool subsampled{format == kRawImageI420};
        for (std::size_t index{}; index < planarTextures.size(); ++index) {
            D3D11_TEXTURE2D_DESC description{};
            description.Width = static_cast<UINT>(index == 0 || !subsampled ? width : (width + 1) / 2);
            description.Height = static_cast<UINT>(index == 0 || !subsampled ? height : (height + 1) / 2);
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_R8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            if (FAILED(device->d3d11_device_->CreateTexture2D(&description, nullptr, planarTextures[index].ReleaseAndGetAddressOf())) ||
                FAILED(device->d3d11_device_->CreateShaderResourceView(planarTextures[index].Get(), nullptr,
                                                                       planeViews[index].ReleaseAndGetAddressOf()))) {
                return false;
            }
        }
        return true;
    }

    bool EnsureTextures(const RawImage& image) {
        if (outputTexture && width == image.img_width && height == image.img_height && format == image.Format())
            return true;
        outputView.Reset();
        outputTarget.Reset();
        outputTexture.Reset();
        nv12Texture.Reset();
        for (auto& texture : planarTextures)
            texture.Reset();
        for (auto& view : planeViews)
            view.Reset();
        width = image.img_width;
        height = image.img_height;
        inputWidth = width;
        inputHeight = height;
        format = image.Format();
        if (!CreateOutput(width, height))
            return false;
        if (format == kRawImageD3D11Texture || format == kRawImageNV12)
            return CreateNv12Input(width, height);
        if (format == kRawImageI420 || format == kRawImageI444)
            return CreatePlanarInput();
        return false;
    }

    bool Upload(const std::shared_ptr<RawImage>& image) {
        auto& context = *device->d3d11_device_context_.Get();
        if (image->Format() == kRawImageD3D11Texture) {
            const auto frame = D3D11FrameOf(image);
            if (!frame || !frame->texture || frame->device_owner != device)
                return false;
            D3D11_TEXTURE2D_DESC sourceDescription{};
            frame->texture->GetDesc(&sourceDescription);
            if (sourceDescription.Format != DXGI_FORMAT_NV12 || sourceDescription.Width < static_cast<UINT>(width) ||
                sourceDescription.Height < static_cast<UINT>(height)) {
                return false;
            }
            if (!nv12Texture || inputWidth != static_cast<int>(sourceDescription.Width) ||
                inputHeight != static_cast<int>(sourceDescription.Height)) {
                if (!CreateNv12Input(static_cast<int>(sourceDescription.Width), static_cast<int>(sourceDescription.Height)))
                    return false;
            }
            // FFmpeg's decoder pool is alignment padded. Copy the complete GPU subresource and crop through texture coordinates, as Moonlight does.
            context.CopySubresourceRegion(nv12Texture.Get(), 0, 0, 0, 0, frame->texture.Get(), frame->subresource, nullptr);
            return true;
        }
        if (image->Format() == kRawImageNV12) {
            const auto y = image->Plane(0);
            const auto uv = image->Plane(1);
            const auto yLayout = image->Layout(0);
            const auto uvLayout = image->Layout(1);
            if (!yLayout || !uvLayout || y.empty() || uv.empty())
                return false;
            std::vector<char> contiguous(static_cast<std::size_t>(width) * height + static_cast<std::size_t>(width) * ((height + 1) / 2));
            for (int row{}; row < height; ++row)
                std::memcpy(contiguous.data() + static_cast<std::size_t>(row) * width, y.data() + static_cast<std::size_t>(row) * yLayout->stride,
                            static_cast<std::size_t>(width));
            const auto uvOffset{static_cast<std::size_t>(width) * height};
            for (int row{}; row < (height + 1) / 2; ++row)
                std::memcpy(contiguous.data() + uvOffset + static_cast<std::size_t>(row) * width,
                            uv.data() + static_cast<std::size_t>(row) * uvLayout->stride, static_cast<std::size_t>(width));
            context.UpdateSubresource(nv12Texture.Get(), 0, nullptr, contiguous.data(), static_cast<UINT>(width), 0);
            return true;
        }
        for (std::size_t index{}; index < planarTextures.size(); ++index) {
            const auto plane = image->Plane(index);
            const auto layout = image->Layout(index);
            if (!layout || !UploadPlane(context, *planarTextures[index].Get(), plane, layout->stride))
                return false;
        }
        return true;
    }

    void Draw() {
        auto& context = *device->d3d11_device_context_.Get();
        const std::array frameCoordinates{static_cast<float>(width) / static_cast<float>(inputWidth),
                                          static_cast<float>(height) / static_cast<float>(inputHeight), 0.0F, 0.0F};
        context.UpdateSubresource(frameCoordinatesBuffer.Get(), 0, nullptr, frameCoordinates.data(), 0, 0);
        constexpr std::array clearColor{0.0F, 0.0F, 0.0F, 1.0F};
        context.OMSetRenderTargets(1, outputTarget.GetAddressOf(), nullptr);
        context.ClearRenderTargetView(outputTarget.Get(), clearColor.data());
        const D3D11_VIEWPORT viewport{0.0F, 0.0F, static_cast<float>(width), static_cast<float>(height), 0.0F, 1.0F};
        context.RSSetViewports(1, &viewport);
        context.IASetInputLayout(nullptr);
        context.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context.VSSetShader(vertexShader.Get(), nullptr, 0);
        context.PSSetShader(format == kRawImageD3D11Texture || format == kRawImageNV12 ? nv12PixelShader.Get() : planarPixelShader.Get(), nullptr, 0);
        context.PSSetConstantBuffers(0, 1, frameCoordinatesBuffer.GetAddressOf());
        context.PSSetSamplers(0, 1, sampler.GetAddressOf());
        for (UINT index{}; index < planeViews.size(); ++index)
            context.PSSetShaderResources(index, 1, planeViews[index].GetAddressOf());
        context.Draw(3, 0);
        ComPtr<ID3D11ShaderResourceView> emptyView{};
        for (UINT index{}; index < planeViews.size(); ++index)
            context.PSSetShaderResources(index, 1, emptyView.GetAddressOf());
    }
};

std::shared_ptr<D3d11VideoPresenter> D3d11VideoPresenter::Create(std::shared_ptr<D3D11DeviceWrapper> device) {
    auto result = std::make_shared<D3d11VideoPresenter>(std::move(device));
    return result->Initialize() ? result : nullptr;
}

D3d11VideoPresenter::D3d11VideoPresenter(std::shared_ptr<D3D11DeviceWrapper> device) : impl_{std::make_unique<Impl>(std::move(device))} {}
D3d11VideoPresenter::~D3d11VideoPresenter() = default;

bool D3d11VideoPresenter::Initialize() {
    if (!impl_->device || !impl_->device->IsValid())
        return false;
    const auto vertex = CompileShader(kVertexShader, "vs_5_0");
    const auto nv12 = CompileShader(kNv12PixelShader, "ps_5_0");
    const auto planar = CompileShader(kPlanarPixelShader, "ps_5_0");
    if (!vertex || !nv12 || !planar) {
        LOGE("D3D11 video shader compilation failed: {}", !vertex ? vertex.error() : (!nv12 ? nv12.error() : planar.error()));
        return false;
    }
    auto& device = *impl_->device->d3d11_device_.Get();
    if (FAILED(device.CreateVertexShader(vertex.value()->GetBufferPointer(), vertex.value()->GetBufferSize(), nullptr,
                                         impl_->vertexShader.ReleaseAndGetAddressOf())) ||
        FAILED(device.CreatePixelShader(nv12.value()->GetBufferPointer(), nv12.value()->GetBufferSize(), nullptr,
                                        impl_->nv12PixelShader.ReleaseAndGetAddressOf())) ||
        FAILED(device.CreatePixelShader(planar.value()->GetBufferPointer(), planar.value()->GetBufferSize(), nullptr,
                                        impl_->planarPixelShader.ReleaseAndGetAddressOf()))) {
        return false;
    }
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_BUFFER_DESC coordinates{};
    coordinates.ByteWidth = sizeof(float) * 4U;
    coordinates.Usage = D3D11_USAGE_DEFAULT;
    coordinates.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    return SUCCEEDED(device.CreateSamplerState(&sampler, impl_->sampler.ReleaseAndGetAddressOf())) &&
           SUCCEEDED(device.CreateBuffer(&coordinates, nullptr, impl_->frameCoordinatesBuffer.ReleaseAndGetAddressOf()));
}

bool D3d11VideoPresenter::Present(const std::shared_ptr<RawImage>& image) {
    if (!image || image->img_width <= 0 || image->img_height <= 0 || !impl_->EnsureTextures(*image) || !impl_->Upload(image))
        return false;
    impl_->Draw();
    return true;
}

std::uint64_t D3d11VideoPresenter::TextureId() const noexcept {
    return reinterpret_cast<std::uint64_t>(impl_->outputView.Get());
}

} // namespace px::desktop
