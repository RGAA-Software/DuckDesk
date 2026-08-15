// GoDesk patch: Win32-desktop process-loopback activation for miniaudio.
//
// Upstream miniaudio (desktop) does:
//   IMMDeviceEnumerator::GetDevice(L"VAD\\Process_Loopback")
// which returns E_INVALIDARG — that string is NOT an MMDevice id.
//
// Correct API (MS ApplicationLoopback / our ProcessLoopbackAudioCapture):
//   ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, ...)
//
// Upstream only wires ActivateAudioInterfaceAsync for MA_WIN32_UWP.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <atomic>

// miniaudio types (implementation already included elsewhere in the TU set)
#include "miniaudio.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace {

class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler,
                              public IAgileObject {
public:
    ActivateHandler() : ev_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~ActivateHandler() {
        if (client_) {
            client_->Release();
        }
        if (ev_) {
            CloseHandle(ev_);
        }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) {
            return E_POINTER;
        }
        if (riid == __uuidof(IUnknown) ||
            riid == __uuidof(IActivateAudioInterfaceCompletionHandler)) {
            *ppv = static_cast<IActivateAudioInterfaceCompletionHandler*>(this);
            AddRef();
            return S_OK;
        }
        if (riid == __uuidof(IAgileObject)) {
            *ppv = static_cast<IAgileObject*>(this);
            AddRef();
            return S_OK;
        }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++ref_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --ref_;
        if (n == 0) {
            delete this;
        }
        return n;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT act = E_FAIL;
        IUnknown* unk = nullptr;
        if (op) {
            op->GetActivateResult(&act, &unk);
        }
        hr_ = act;
        if (SUCCEEDED(act) && unk) {
            unk->QueryInterface(IID_PPV_ARGS(&client_));
            unk->Release();
        } else if (unk) {
            unk->Release();
        }
        SetEvent(ev_);
        return S_OK;
    }

    bool Wait(DWORD ms) { return WaitForSingleObject(ev_, ms) == WAIT_OBJECT_0; }
    HRESULT hr() const { return hr_; }
    IAudioClient* TakeClient() {
        IAudioClient* c = client_;
        client_ = nullptr;
        return c;
    }

private:
    std::atomic<ULONG> ref_{1};
    HANDLE ev_ = nullptr;
    HRESULT hr_ = E_FAIL;
    IAudioClient* client_ = nullptr;
};

}  // namespace

// Called from patched miniaudio.h (desktop process-loopback branch).
extern "C" ma_result ma_godsk_get_IAudioClient_process_loopback(
    ma_context* /*pContext*/,
    const wchar_t* device_path,
    void* activation_params_propvariant, /* MA_PROPVARIANT* / PROPVARIANT* */
    void** pp_audio_client               /* ma_IAudioClient** */
) {
    if (!device_path || !activation_params_propvariant || !pp_audio_client) {
        return MA_INVALID_ARGS;
    }
    *pp_audio_client = nullptr;

    auto* pv = reinterpret_cast<PROPVARIANT*>(activation_params_propvariant);
    auto* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(device_path, __uuidof(IAudioClient), pv, handler, &op);
    if (FAILED(hr)) {
        handler->Release();
        return (hr == E_INVALIDARG) ? MA_INVALID_ARGS : MA_ERROR;
    }
    if (!handler->Wait(15000) || FAILED(handler->hr())) {
        const HRESULT ahr = handler->hr();
        if (op) {
            op->Release();
        }
        handler->Release();
        return (ahr == E_INVALIDARG) ? MA_INVALID_ARGS : MA_ERROR;
    }
    if (op) {
        op->Release();
    }
    IAudioClient* client = handler->TakeClient();
    handler->Release();
    if (!client) {
        return MA_ERROR;
    }
    *pp_audio_client = reinterpret_cast<void*>(client);
    return MA_SUCCESS;
}
