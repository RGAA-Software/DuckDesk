#pragma once

#include <sddl.h>

#include <array>
#include <thread>

#include <atlbase.h>
#include <atlfile.h>
#include <d3d11.h>

#include "easyhook/easyhook.h"
#include "hook_dxgi.h"
#include "hk_utils/shared_mem_info.h"

using namespace tc;

struct SharedTexture {
    CComPtr<ID3D11Texture2D> texture;
    HANDLE handle;
};

class CaptureTex {
public:
    bool Run() noexcept;

    void Free() noexcept;

    bool AttemptToHook() noexcept;

    [[nodiscard]] const SECURITY_ATTRIBUTES *GetSA() const noexcept {
        assert(nullptr != sa_.lpSecurityDescriptor);
        return &sa_;
    }

    SECURITY_ATTRIBUTES *SA() noexcept {
        assert(nullptr != sa_.lpSecurityDescriptor);
        return &sa_;
    }

    [[nodiscard]] HANDLE GetStopEvent() const noexcept {
        assert(nullptr != stop_event_);
        return stop_event_;
    }

    bool IsEncoderStarted() const noexcept { return is_encoder_started_; }

    bool IsPresentEnabled() const noexcept { return is_present_enabled_; }

    bool CreateSharedFrameInfo() noexcept {
        if (nullptr == shared_frame_info_) {
            HRESULT hr = shared_frame_info_.MapSharedMem(
                    sizeof(SharedVideoFrameInfo),
                    kSharedVideoFrameInfoFileMappingName.data(), nullptr, SA());
            if (FAILED(hr)) {
                ATLTRACE2(atlTraceException, 0,
                          "MapSharedMem(info) failed with 0x%08x.\n", hr);
                return false;
            }
        }
        return true;
    }

//    SharedVideoFrameInfo *GetSharedVideoFrameInfo() const noexcept {
//        assert(nullptr != shared_frame_info_);
//        return shared_frame_info_;
//    }

    //void FreeSharedVideoFrameInfo() noexcept { shared_frame_info_.Unmap(); }

//    PackedVideoTextureFrame *GetPackedVideoTextureFrame(size_t index) const noexcept {
//        assert(index < kNumberOfSharedFrames);
//        auto shared_frame = static_cast<SharedVideoTextureFrames *>(shared_frames_.GetData());
//        return static_cast<PackedVideoTextureFrame *>(shared_frame->frames) + index;
//    }

//    PackedVideoTextureFrame *GetAvailablePackedVideoTextureFrame() const noexcept {
//        return GetPackedVideoTextureFrame(frame_count_ % kNumberOfSharedFrames);
//    }

//    size_t GetFrameCount() const noexcept { return frame_count_; }

//    void SetSharedFrameReadyEvent() noexcept {
//        ++frame_count_;
//        SetEvent(shared_frame_ready_event_);
//    }

    void FreeSharedVideoTextureFrames() noexcept {
        shared_frames_.Unmap();
        shared_frame_ready_event_.Close();
    }

    // bool ShareTexture(ID3D11Texture2D *new_texture, ID3D11Device *device, ID3D11DeviceContext *context) noexcept;

    void FreeSharedTexture() noexcept;

private:
    bool Initialize() noexcept;
    int HookThread() noexcept;

private:
    SECURITY_ATTRIBUTES sa_{};
    CHandle stop_event_;
    std::thread hook_thread_;

    CAtlFileMapping<SharedVideoFrameInfo> shared_frame_info_;
    CAtlFileMapping<SharedVideoTextureFrames> shared_frames_;
    CHandle shared_frame_ready_event_;
    size_t frame_count_{0};

    std::array<SharedTexture, tc::kNumberOfSharedFrames> shared_textures_;
    std::uint64_t texture_id_{0};

    CHandle encoder_started_event_;
    bool is_encoder_started_{false};

    bool is_present_enabled_{true};

    // TODO: D3d9 hook
    // bool is_d3d9_hooked_{false};
    // HookD3d9 hook_d3d9_;

    bool is_dxgi_hooked_{false};
    HookD3D11 hook_dxgi_;

    //CaptureD3D12 capture_d3d12;

};

extern CaptureTex g_capture_tex;
