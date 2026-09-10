#include <Windows.h>
#include <atlbase.h>
#include <d3d11.h>

#include "product-frame.h"
#include "hook_manager.h"
#include "px_capture/capture_message_maker.h"
#include "ws_ipc_client.h"

extern "C" bool px_graphics_ready(void) {
    return px::HookManager::Instance()->graphics_ready_.load(std::memory_order_acquire);
}

// The OBS C backend owns its shared texture. Retain COM lifetime immediately; no borrowed value crosses the IPC queue.
extern "C" bool px_publish_shared_frame(ID3D11Texture2D* texture) { // NOLINT(gammaray-raw-pointer-boundary) OBS C ABI.
    const CComPtr<ID3D11Texture2D> retained{texture};
    if (!retained) {
        return false;
    }
    const auto manager = px::HookManager::Instance();
    const auto ipc = manager->ws_ipc_client_;
    if (!ipc || !ipc->IsConnected()) {
        return false;
    }
    CComPtr<ID3D11Device> device{};
    retained->GetDevice(&device);
    CComPtr<ID3D11DeviceContext> context{};
    device->GetImmediateContext(&context);
    const CComQIPtr<IDXGIDevice> dxgi_device{device};
    const CComQIPtr<IDXGIResource> resource{retained};
    CComPtr<IDXGIAdapter> adapter{};
    DXGI_ADAPTER_DESC adapter_desc{};
    HANDLE shared_handle{}; // NOLINT(gammaray-raw-pointer-boundary) Borrowed DXGI handle; lifetime belongs to retained texture.
    if (!dxgi_device || !resource || FAILED(dxgi_device->GetAdapter(&adapter)) || FAILED(adapter->GetDesc(&adapter_desc)) ||
        FAILED(resource->GetSharedHandle(&shared_handle)) || !shared_handle) {
        return false;
    }
    D3D11_TEXTURE2D_DESC description{};
    retained->GetDesc(&description);
    px::IpcCaptureVideoFrame frame{};
    frame.capture_type_ = px::kCaptureVideoByHandle;
    frame.frame_width_ = description.Width;
    frame.frame_height_ = description.Height;
    frame.frame_format_ = description.Format;
    frame.handle_ = reinterpret_cast<std::uintptr_t>(shared_handle);
    frame.frame_index_ = manager->AppendFrameIndex();
    frame.adapter_uid_ = static_cast<std::int64_t>((static_cast<std::uint64_t>(static_cast<std::uint32_t>(adapter_desc.AdapterLuid.HighPart)) << 32) |
                                                   adapter_desc.AdapterLuid.LowPart);
    // NV interop unlock precedes this call. Submit writes before notifying the consuming D3D device.
    context->Flush();
    manager->Send(px::CaptureMessageMaker::ConvertMessageToString(frame));
    return true;
}
