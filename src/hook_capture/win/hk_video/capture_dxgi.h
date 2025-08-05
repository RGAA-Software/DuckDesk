#pragma once

#include <d3d11.h>
#include "tc_common_new/log.h"

class CaptureDxgi {
public:
    [[nodiscard]] const IDXGISwapChain *GetSwapChain() const noexcept { return swap_; }

    void Reset() noexcept {
        swap_ = nullptr;
        capture_ = nullptr;
        if (nullptr != free_) {
            free_();
            free_ = nullptr;
        }
    }

    bool Setup(IDXGISwapChain *swap) noexcept;

    [[nodiscard]] bool CanCapture() const noexcept { return nullptr != capture_; }

    void Capture(IDXGISwapChain *swap) const noexcept {
        assert(nullptr != capture_);
        CComPtr<IDXGIResource> back_buffer;

        HRESULT hr = swap->GetBuffer(0, __uuidof(IUnknown), reinterpret_cast<void **>(&back_buffer));
        if (SUCCEEDED(hr)) {
            capture_(swap, back_buffer);
        } else {
            LOGE("Swap->GetBuffer failed.");
        }
    }

    void Free() const noexcept {
        if (nullptr != free_) {
            free_();
        }
    }

private:

    IDXGISwapChain *swap_ = nullptr;
    void (*capture_)(void *, void *) = nullptr;
    void (*free_)() = nullptr;
};
