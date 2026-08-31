#include "InProcessLoopbackCapture.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <atomic>

#include "AudioShare.h"
#include "px_common_new/data.h"
#include "px_common_new/log.h"

namespace px {
namespace {

class ActivateHandler final : public IActivateAudioInterfaceCompletionHandler,
                              public IAgileObject {
public:
    ActivateHandler()
        : activate_done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          sample_ready_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

    ~ActivateHandler() {
        if (cap_) {
            cap_->Release();
        }
        if (client_) {
            client_->Release();
        }
        if (sample_ready_) {
            CloseHandle(sample_ready_);
        }
        if (activate_done_) {
            CloseHandle(activate_done_);
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
    ULONG STDMETHODCALLTYPE AddRef() override { return ref_.fetch_add(1) + 1; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = ref_.fetch_sub(1) - 1;
        if (n == 0) {
            delete this;
        }
        return n;
    }

    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* op) override {
        activate_hr_ = E_FAIL;
        HRESULT hr_activate = E_FAIL;
        IUnknown* unk = nullptr;
        if (!op) {
            SetEvent(activate_done_);
            return S_OK;
        }
        op->GetActivateResult(&hr_activate, &unk);
        if (FAILED(hr_activate) || !unk) {
            activate_hr_ = FAILED(hr_activate) ? hr_activate : E_FAIL;
            SetEvent(activate_done_);
            return S_OK;
        }

        HRESULT hr = unk->QueryInterface(IID_PPV_ARGS(&client_));
        unk->Release();
        if (FAILED(hr) || !client_) {
            activate_hr_ = hr;
            SetEvent(activate_done_);
            return S_OK;
        }

        format_ = {};
        format_.wFormatTag = WAVE_FORMAT_PCM;
        format_.nChannels = 2;
        format_.nSamplesPerSec = 44100;
        format_.wBitsPerSample = 16;
        format_.nBlockAlign = format_.nChannels * format_.wBitsPerSample / 8;
        format_.nAvgBytesPerSec = format_.nSamplesPerSec * format_.nBlockAlign;
        format_.cbSize = 0;

        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags, 200000, 0, &format_, nullptr);
        if (FAILED(hr)) {
            activate_hr_ = hr;
            SetEvent(activate_done_);
            return S_OK;
        }

        hr = client_->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void**>(&cap_));
        if (FAILED(hr) || !cap_) {
            activate_hr_ = FAILED(hr) ? hr : E_FAIL;
            SetEvent(activate_done_);
            return S_OK;
        }

        hr = client_->SetEventHandle(sample_ready_);
        if (FAILED(hr)) {
            activate_hr_ = hr;
            SetEvent(activate_done_);
            return S_OK;
        }

        activate_hr_ = S_OK;
        SetEvent(activate_done_);
        return S_OK;
    }

    bool WaitActivate(DWORD ms) {
        return WaitForSingleObject(activate_done_, ms) == WAIT_OBJECT_0;
    }
    HRESULT activate_hr() const { return activate_hr_; }
    IAudioClient* client() const { return client_; }
    IAudioCaptureClient* capture() const { return cap_; }
    HANDLE sample_event() const { return sample_ready_; }
    const WAVEFORMATEX& format() const { return format_; }

private:
    std::atomic<ULONG> ref_{1};
    HANDLE activate_done_ = nullptr;
    HANDLE sample_ready_ = nullptr;
    HRESULT activate_hr_ = E_FAIL;
    IAudioClient* client_ = nullptr;
    IAudioCaptureClient* cap_ = nullptr;
    WAVEFORMATEX format_{};
};

}  // namespace

struct InProcessLoopbackCapture::State {
    std::shared_ptr<AudioShare> share;
    std::atomic_bool stop{false};
    std::atomic_bool running{false};
};

InProcessLoopbackCapture::~InProcessLoopbackCapture() {
    Stop();
}

bool InProcessLoopbackCapture::Start(std::shared_ptr<AudioShare> share, uint32_t pid) {
    if (thread_.joinable() || !share || pid == 0) {
        return false;
    }
    state_ = std::make_shared<State>();
    state_->share = std::move(share);
    // Async: do not wait for activate. Init often runs while the game is still
    // CREATE_SUSPENDED; ThreadMain delays then activates after the game is live.
    const auto state = state_;
    thread_ = std::thread([state, pid] { ThreadMain(state, pid); });
    return true;
}

void InProcessLoopbackCapture::Stop() {
    const auto state = state_;
    if (state) {
        state->stop.store(true, std::memory_order_release);
    }
    if (thread_.joinable()) {
        thread_.join();
    }
    if (state) {
        state->running.store(false, std::memory_order_release);
        state->share.reset();
    }
    state_.reset();
}

bool InProcessLoopbackCapture::running() const {
    const auto state = state_;
    return state && state->running.load(std::memory_order_acquire);
}

void InProcessLoopbackCapture::ThreadMain(
    const std::shared_ptr<State>& state, uint32_t pid) {
    // Game is often still CREATE_SUSPENDED when the DLL loads. Wait so the audio
    // engine has real output before activating process-loopback (early activate
    // has been observed to yield an all-silent capture for the rest of the session).
    for (int i = 0;
         i < 120 && !state->stop.load(std::memory_order_acquire);
         ++i) {
        Sleep(100);  // ~12s total
    }
    if (state->stop.load(std::memory_order_acquire)) {
        return;
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool need_uninit = SUCCEEDED(hr);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        LOGE("InProcLoopback CoInitializeEx failed: 0x{:08x}", static_cast<unsigned>(hr));
        return;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&ap);

    auto* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* async_op = nullptr;
    hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                     __uuidof(IAudioClient), &pv, handler, &async_op);
    if (FAILED(hr)) {
        LOGE("InProcLoopback ActivateAudioInterfaceAsync failed: 0x{:08x}",
             static_cast<unsigned>(hr));
        handler->Release();
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }

    if (!handler->WaitActivate(15000) || FAILED(handler->activate_hr())) {
        LOGE("InProcLoopback activate/init failed: 0x{:08x}",
             static_cast<unsigned>(handler->activate_hr()));
        if (async_op) {
            async_op->Release();
        }
        handler->Release();
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }
    if (async_op) {
        async_op->Release();
    }

    IAudioClient* client = handler->client();
    IAudioCaptureClient* cap = handler->capture();
    const WAVEFORMATEX& fmt = handler->format();
    if (!client || !cap || !state->share) {
        handler->Release();
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }

    state->share->SetAudioFormat(
        SimpleAudioFormat::kPCM_S16,
        static_cast<int>(fmt.nSamplesPerSec),
        static_cast<int>(fmt.nChannels), 16);

    if (state->stop.load(std::memory_order_acquire)) {
        handler->Release();
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }

    hr = client->Start();
    if (FAILED(hr)) {
        LOGE("InProcLoopback Start failed: 0x{:08x}", static_cast<unsigned>(hr));
        handler->Release();
        if (need_uninit) {
            CoUninitialize();
        }
        return;
    }

    state->running.store(true, std::memory_order_release);
    LOGI("InProcLoopback capturing pid={} {}Hz {}ch", pid, fmt.nSamplesPerSec, fmt.nChannels);

    uint64_t packets = 0;
    while (!state->stop.load(std::memory_order_acquire)) {
        WaitForSingleObject(handler->sample_event(), 50);
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            if (frames > 0 && data
                && (flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0
                && state->share) {
                const int bytes =
                    static_cast<int>(frames) * static_cast<int>(fmt.nBlockAlign);
                if (bytes > 0) {
                    state->share->PostAudioData(
                        Data::Make(reinterpret_cast<const char*>(data), bytes));
                    packets++;
                }
            }
            cap->ReleaseBuffer(frames);
        }
        if (packets > 0 && (packets % 200) == 0) {
            LOGI("InProcLoopback packets={}", packets);
        }
    }

    client->Stop();
    state->running.store(false, std::memory_order_release);
    LOGI("InProcLoopback stopped packets={}", packets);
    handler->Release();
    if (need_uninit) {
        CoUninitialize();
    }
}

}  // namespace px
