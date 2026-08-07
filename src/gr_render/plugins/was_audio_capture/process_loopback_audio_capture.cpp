#include "process_loopback_audio_capture.h"

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
#include <chrono>
#include <string>

#include "tc_common_new/data.h"
#include "tc_common_new/log.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace tc {
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

        // Microsoft ApplicationLoopback hard-coded capture format.
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

AudioCapturePtr ProcessLoopbackAudioCapture::Make(uint32_t process_id) {
    return std::make_shared<ProcessLoopbackAudioCapture>(process_id);
}

ProcessLoopbackAudioCapture::ProcessLoopbackAudioCapture(uint32_t process_id)
    : pid_(process_id) {}

ProcessLoopbackAudioCapture::~ProcessLoopbackAudioCapture() {
    Stop();
}

int ProcessLoopbackAudioCapture::Start() {
    if (pid_ == 0) {
        LOGE("[ProcessLoopback] Start failed: pid=0");
        return -1;
    }
    {
        std::lock_guard lock(mu_);
        if (running_.load() || worker_.joinable()) {
            return running_.load() ? 0 : -3;
        }
        want_running_ = true;
        worker_ = std::thread([this] { CaptureThreadMain(); });
    }
    // Wait for activate success/fail so caller can trust IsProviding().
    for (int i = 0; i < 200 && want_running_.load(); ++i) {
        if (running_.load()) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!running_.load()) {
        Stop();
        LOGE("[ProcessLoopback] Start failed/timeout pid={}", pid_);
        return -2;
    }
    return 0;
}

int ProcessLoopbackAudioCapture::Pause() {
    return Stop();
}

int ProcessLoopbackAudioCapture::Stop() {
    want_running_ = false;
    std::thread joined;
    {
        std::lock_guard lock(mu_);
        if (worker_.joinable()) {
            joined = std::move(worker_);
        }
    }
    if (joined.joinable()) {
        joined.join();
    }
    running_ = false;
    if (stop_callback_) {
        stop_callback_();
    }
    return 0;
}

void ProcessLoopbackAudioCapture::CaptureThreadMain() {
    const HRESULT co_hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(co_hr) && co_hr != S_FALSE && co_hr != RPC_E_CHANGED_MODE) {
        LOGE("[ProcessLoopback] CoInitializeEx failed 0x{:08x}", static_cast<unsigned>(co_hr));
        return;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = pid_;
    ap.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&ap);

    auto* handler = new ActivateHandler();
    IActivateAudioInterfaceAsyncOperation* async_op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &pv, handler, &async_op);
    if (FAILED(hr)) {
        LOGE("[ProcessLoopback] ActivateAudioInterfaceAsync failed 0x{:08x} pid={}",
             static_cast<unsigned>(hr), pid_);
        handler->Release();
        return;
    }
    if (!handler->WaitActivate(15000) || FAILED(handler->activate_hr())) {
        LOGE("[ProcessLoopback] activate/init failed 0x{:08x} pid={}",
             static_cast<unsigned>(handler->activate_hr()), pid_);
        if (async_op) {
            async_op->Release();
        }
        handler->Release();
        return;
    }
    if (async_op) {
        async_op->Release();
    }

    IAudioClient* client = handler->client();
    IAudioCaptureClient* cap = handler->capture();
    const WAVEFORMATEX& fmt = handler->format();
    samples_ = static_cast<int>(fmt.nSamplesPerSec);
    channels_ = static_cast<int>(fmt.nChannels);
    bits_ = static_cast<int>(fmt.wBitsPerSample);

    if (format_callback_) {
        format_callback_(samples_, channels_, bits_);
    }

    hr = client->Start();
    if (FAILED(hr)) {
        LOGE("[ProcessLoopback] client->Start failed 0x{:08x}", static_cast<unsigned>(hr));
        handler->Release();
        return;
    }

    running_ = true;
    LOGI("[ProcessLoopback] capturing pid={} {}Hz {}ch {}bit", pid_, samples_, channels_, bits_);

    uint64_t packets = 0;
    while (want_running_.load()) {
        WaitForSingleObject(handler->sample_event(), 50);
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            if (frames > 0 && data && (flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0 &&
                data_callback_) {
                const int bytes =
                    static_cast<int>(frames) * channels_ * (bits_ / 8);
                data_callback_(Data::Make(reinterpret_cast<const char*>(data), bytes));
                ++packets;
                if (packets == 1 || (packets % 200) == 0) {
                    LOGI("[ProcessLoopback] packets={} last_bytes={}", packets, bytes);
                }
            }
            cap->ReleaseBuffer(frames);
        }
    }

    client->Stop();
    running_ = false;
    handler->Release();
    LOGI("[ProcessLoopback] stopped pid={} packets={}", pid_, packets);
}

}  // namespace tc
