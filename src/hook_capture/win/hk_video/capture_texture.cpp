#include <process.h>
#include <crtdefs.h>
#include "framework.h"

#include "capture_texture.h"

#include <d3d11.h>
#include <format>
#include "tc_common_new/log.h"
#include "hk_utils/shared_mem_info.h"

using namespace tc;

bool CaptureTex::Initialize() noexcept {

    if (nullptr == sa_.lpSecurityDescriptor) {
        if (!ConvertStringSecurityDescriptorToSecurityDescriptor(
                _T("D:P(A;;GA;;;WD)"), SDDL_REVISION_1, &sa_.lpSecurityDescriptor,
                nullptr)) {
            LOGE("ConvertStringSecurityDescriptorToSecurityDescriptor FAILED.");
            return false;
        }
    }

    if (nullptr == stop_event_) {
        HANDLE ev = CreateEvent(SA(), TRUE, FALSE, nullptr);
        if (nullptr == ev) {
            LOGE("CreateEvent FAILED: stop_event");
            return false;
        }
        stop_event_.Attach(ev);
    }

    if (nullptr == encoder_started_event_) {
        HANDLE ev = CreateEvent(SA(), TRUE, FALSE, kVideoStartedEventName.data());
        if (nullptr == ev) {
            LOGE("CreateEvent FAILED: encoder_started_event");
            return false;
        }
        encoder_started_event_.Attach(ev);
    }

    if (!CreateSharedFrameInfo()) {
        LOGE("CreateSharedFrameInfo FAILED.");
        return false;
    }
    return true;
}

bool CaptureTex::Run() noexcept {

    LOGI("CaptureTex Run ...");
    if (!Initialize()) {
        LOGE("CaptureTex Init failed !");
        return false;
    }
    LOGI("CaptureTex Initialize success.");

    if (nullptr != hook_thread_.native_handle() &&
        WAIT_TIMEOUT == WaitForSingleObject(hook_thread_.native_handle(), 0)) {
        LOGI("Already running...");
        return true;
    }

    hook_thread_ = std::thread(&CaptureTex::HookThread, this);
    if (!hook_thread_.joinable()) {
        LOGE("Join failed !");
        return false;
    }
    return true;
}

void CaptureTex::Free() noexcept {
    ATLTRACE2(atlTraceUtil, 0, "%s: +\n", __func__);

    SetEvent(stop_event_);
    if (hook_thread_.joinable()) {
        hook_thread_.join();
    }

    if (is_dxgi_hooked_) {
        hook_dxgi_.Unhook();
    }

    FreeSharedVideoTextureFrames();
    //FreeSharedVideoFrameInfo();

    encoder_started_event_.Close();
    stop_event_.Close();

    if (nullptr != sa_.lpSecurityDescriptor) {
        LocalFree(sa_.lpSecurityDescriptor);
        sa_.lpSecurityDescriptor = nullptr;
    }

    FreeSharedTexture();

    ATLTRACE2(atlTraceUtil, 0, "%s: -\n", __func__);
}

bool CaptureTex::AttemptToHook() noexcept {
    bool hooked = false;

    LOGI("is_dxgi_hooked : " + std::to_string(is_dxgi_hooked_));

    if (!is_dxgi_hooked_) {
        if (hook_dxgi_.Hook()) {
            is_dxgi_hooked_ = true;
            hooked = true;
        }
    }

    //if (!capture_d3d12.IsHooked()) {
    //	capture_d3d12.Init();
    //}

    return hooked;
}

int CaptureTex::HookThread() noexcept {
    CHandle donot_present_event;
    HANDLE ev = CreateEvent(SA(), TRUE, FALSE, kDoNotPresentEventName.data());
    if (nullptr == ev) {
        LOGI("HookThread CreateEvent failed .");
    } else {
        donot_present_event.Attach(ev);
    }

    LOGI("HookThread will CreateEvent");

    CHandle encoder_stopped_event;
    ev = CreateEvent(SA(), TRUE, FALSE, kVideoStoppedEventName.data());
    if (nullptr == ev) {
        LOGI("HookThread CreateEvent failed");
    } else {
        encoder_stopped_event.Attach(ev);
    }

    for (bool hooked = false;;) {
        HANDLE started_events[] = {stop_event_, encoder_started_event_};
        auto wait = WAIT_OBJECT_0 + 1;

        if (WAIT_OBJECT_0 == wait) {
            ATLTRACE2(atlTraceUtil, 0, "%s: stopping.\n", __func__);
            LOGI("wait 1");
            return 0;
        } else if (WAIT_OBJECT_0 + 1 == wait) {
            is_encoder_started_ = true;
            ATLTRACE2(atlTraceUtil, 0, "%s: Video encoder is started.\n", __func__);
            LOGI("wait 2");
        } else {
            LOGI("wait 3");
            int error_code = GetLastError();
            ATLTRACE2(atlTraceException, 0,
                      "%s: WaitForMultipleObjects() return %u, error %u.\n", __func__,
                      wait, error_code);

            LOGI("Hook thread...error !" + std::to_string(error_code));

            return error_code;
        }

        if (nullptr != donot_present_event) {
            wait = WaitForSingleObject(donot_present_event, 0);
            if (WAIT_OBJECT_0 == wait) {
                is_present_enabled_ = false;
            } else {
                is_present_enabled_ = true;
            }

            LOGI("donot_present_event ...");

            ATLTRACE2(atlTraceUtil, 0,
                      "%s: wait donot_present_event = %d, is_present_enabled = %d.\n",
                      __func__, wait, is_present_enabled_);
        }

        LOGI("hooked : " + std::to_string(hooked));

        HANDLE stop_events[] = {stop_event_, encoder_stopped_event};
        if (!hooked) {
            for (;;) {
                if (AttemptToHook()) {
                    hooked = true;
                    break;
                }
                wait = WaitForMultipleObjects(_countof(stop_events), stop_events, FALSE,
                                              300);
                if (WAIT_OBJECT_0 == wait) {
                    ATLTRACE2(atlTraceUtil, 0, "%s: stopping.\n", __func__);
                    return 0;
                } else if (WAIT_OBJECT_0 + 1 == wait) {
                    break;
                }
            }
        }

        wait = WaitForMultipleObjects(_countof(stop_events), stop_events, FALSE,
                                      INFINITE);
        is_present_enabled_ = true;
        if (WAIT_OBJECT_0 == wait) {
            ATLTRACE2(atlTraceUtil, 0, "%s: stopping.\n", __func__);
            return 0;
        } else if (WAIT_OBJECT_0 + 1 == wait) {
            is_encoder_started_ = false;
            ATLTRACE2(atlTraceUtil, 0, "%s: Video encoder is stopped.\n", __func__);
        } else {
            int error_code = GetLastError();
            ATLTRACE2(atlTraceException, 0,
                      "%s: WaitForMultipleObjects() return %u, error %u.\n", __func__,
                      wait, error_code);
            return error_code;
        }
    }

    ATLTRACE2(atlTraceUtil, 0, "%s: -\n", __func__);
    return -1;
}

//bool CaptureTex::ShareTexture(ID3D11Texture2D *new_texture,
//                              ID3D11Device *device,
//                              ID3D11DeviceContext *context) noexcept {
//    if (nullptr == new_texture || nullptr == device || nullptr == context) {
//        return false;
//    }
//
//    D3D11_TEXTURE2D_DESC new_texture_desc;
//    new_texture->GetDesc(&new_texture_desc);
//
//    bool should_share_texture = false;
//    D3D11_TEXTURE2D_DESC shared_texture_desc;
//    size_t index = frame_count_ % kNumberOfSharedFrames;
//    if (shared_textures_[index].texture_) {
//        shared_textures_[index].texture_->GetDesc(&shared_texture_desc);
//        if (shared_texture_desc.Width != new_texture_desc.Width ||
//            shared_texture_desc.Height != new_texture_desc.Height) {
//            new_texture->GetDesc(&shared_texture_desc);
//            should_share_texture = true;
//        }
//    } else {
//        new_texture->GetDesc(&shared_texture_desc);
//        should_share_texture = true;
//    }
//
//    if (should_share_texture) {
//        shared_texture_desc.Usage = D3D11_USAGE_DEFAULT;
//        shared_texture_desc.MiscFlags |=
//                D3D11_RESOURCE_MISC_SHARED_NTHANDLE | D3D11_RESOURCE_MISC_SHARED;
//        CComPtr<ID3D11Texture2D> shared_texture;
//        HRESULT hr =
//                device->CreateTexture2D(&shared_texture_desc, NULL, &shared_texture);
//        if (FAILED(hr)) {
//            ATLTRACE2(atlTraceException, 0,
//                      "!CreateTexture2D(D3D11_RESOURCE_MISC_SHARED_NTHANDLE) [%d], "
//                      "#0x%08X, #0x%08X\n",
//                      index, hr, device->GetDeviceRemovedReason());
//            return false;
//        }
//
//        CComPtr<IDXGIResource1> shared_resource;
//        hr = shared_texture->QueryInterface(IID_PPV_ARGS(&shared_resource));
//        if (FAILED(hr)) {
//            ATLTRACE2(atlTraceException, 0,
//                      "!QueryInterface(IDXGIResource1) [%d], #0x%08X\n", index, hr);
//            return false;
//        }
//
//        auto instance_id = GetCurrentProcessId();
//        std::wstring shared_handle_name =
//                std::format(kSharedTextureHandleNameFormat, instance_id, texture_id_);
//        ATLTRACE2(atlTraceUtil, 0, L"shared_handle_name %s\n",
//                  shared_handle_name.data());
//
//        HANDLE shared_handle = nullptr;
//        hr = shared_resource->CreateSharedHandle(
//                NULL, DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
//                shared_handle_name.data(), &shared_handle);
//        if (FAILED(hr)) {
//            ATLTRACE2(atlTraceException, 0, "!CreateSharedHandle(), #0x%08X\n", hr);
//            return false;
//        }
//
//        if (shared_textures_[index].handle) {
//            CloseHandle(shared_textures_[index].handle);
//        }
//        shared_textures_[index].handle = shared_handle;
//        shared_textures_[index].texture_ = shared_texture;
//
//        auto frame = GetAvailablePackedVideoTextureFrame();
//        frame->instance_id = instance_id;
//        frame->texture_id = texture_id_++;
//        ATLTRACE2(atlTraceUtil, 0,
//                  L"Update SharedHandle[%d] shared_handle_name: %s\n", index,
//                  shared_handle_name.data());
//    }
//    context->CopyResource(shared_textures_[index].texture_, new_texture);
//    return true;
//}

void CaptureTex::FreeSharedTexture() noexcept {
    for (auto &s: shared_textures_) {
        if (s.handle) {
            CloseHandle(s.handle);
            s.handle = nullptr;
        }
        s.texture.Release();
    }
}
