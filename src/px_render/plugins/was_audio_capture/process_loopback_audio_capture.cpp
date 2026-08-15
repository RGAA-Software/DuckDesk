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
#include <cstring>
#include <string>

#include "px_common_new/data.h"
#include "px_common_new/log.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace px {
namespace {

// Pairs CoUninitialize with CoInitializeEx on thread exit (S_OK/S_FALSE only;
// RPC_E_CHANGED_MODE means another component owns the apartment and our call
// did not increment the ref count, so it must not be balanced).
class CoInitGuard {
public:
    CoInitGuard() : hr_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~CoInitGuard() {
        if (hr_ == S_OK || hr_ == S_FALSE) {
            CoUninitialize();
        }
    }
    bool ok() const {
        return SUCCEEDED(hr_) || hr_ == S_FALSE || hr_ == RPC_E_CHANGED_MODE;
    }
    HRESULT hr() const { return hr_; }

private:
    HRESULT hr_;
};

// Device errors that will never recover inside this capture session: exit the
// capture thread instead of spinning forever.
bool IsFatalCaptureError(HRESULT hr) {
    return hr == AUDCLNT_E_DEVICE_INVALIDATED ||
           hr == AUDCLNT_E_SERVICE_NOT_RUNNING ||
           hr == AUDCLNT_E_RESOURCES_INVALIDATED;
}

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
        // Self-reference: the capture thread took an extra AddRef when activation
        // was kicked off; it is released here so a late callback (after a
        // WaitActivate timeout) can never run against a deleted handler.
        struct SelfRef {
            ActivateHandler* h;
            ~SelfRef() { h->Release(); }
        } self_ref{this};

        if (cancelled_.load()) {
            // Waiter already gave up: do not touch any external state, just
            // drop the self-reference (this may delete the handler).
            return S_OK;
        }

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

        // Client capture format requested from the loopback engine. 48k/2ch/s16:
        // the downstream Opus encoder only accepts 8k/12k/16k/24k/48k sample
        // rates, and AUTOCONVERTPCM makes the engine convert to this format.
        format_ = {};
        format_.wFormatTag = WAVE_FORMAT_PCM;
        format_.nChannels = 2;
        format_.nSamplesPerSec = 48000;
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

        // With AUTOCONVERTPCM, GetBuffer delivers frames already converted to the
        // requested client format above; log the engine mix format for diagnosis.
        WAVEFORMATEX* mix = nullptr;
        hr = client_->GetMixFormat(&mix);
        if (SUCCEEDED(hr) && mix) {
            LOGI("[ProcessLoopback] mix format: tag={} {}Hz {}ch {}bit align={}",
                 mix->wFormatTag, mix->nSamplesPerSec, mix->nChannels,
                 mix->wBitsPerSample, mix->nBlockAlign);
            CoTaskMemFree(mix);
        } else {
            LOGW("[ProcessLoopback] GetMixFormat failed 0x{:08x}; using requested format",
                 static_cast<unsigned>(hr));
        }

        // Never memcpy under a wrong assumption: if the negotiated client format
        // is not PCM16, fail explicitly instead of capturing garbage.
        if (format_.wFormatTag != WAVE_FORMAT_PCM || format_.wBitsPerSample != 16) {
            LOGE("[ProcessLoopback] unsupported client format: tag={} bits={} (expect PCM16)",
                 format_.wFormatTag, format_.wBitsPerSample);
            activate_hr_ = E_FAIL;
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
    // Marks the wait as given up: a late ActivateCompleted must not touch state.
    void Cancel() {
        cancelled_ = true;
        SetEvent(activate_done_);
    }
    HRESULT activate_hr() const { return activate_hr_; }
    IAudioClient* client() const { return client_; }
    IAudioCaptureClient* capture() const { return cap_; }
    HANDLE sample_event() const { return sample_ready_; }
    const WAVEFORMATEX& format() const { return format_; }

private:
    std::atomic<ULONG> ref_{1};
    std::atomic<bool> cancelled_{false};
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
            // Idempotent: already running, or the worker is still starting up.
            LOGI("[ProcessLoopback] Start ignored: {} pid={}",
                 running_.load() ? "already running" : "already starting", pid_);
            return 0;
        }
        want_running_ = true;
        stop_notified_ = false;
        fatal_stop_ = false;
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
    NotifyStopOnce();
    return 0;
}

void ProcessLoopbackAudioCapture::NotifyStopOnce() {
    if (!stop_notified_.exchange(true) && stop_callback_) {
        stop_callback_();
    }
}

void ProcessLoopbackAudioCapture::CaptureThreadMain() {
    const CoInitGuard co_init;
    if (!co_init.ok()) {
        LOGE("[ProcessLoopback] CoInitializeEx failed 0x{:08x}",
             static_cast<unsigned>(co_init.hr()));
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
    // Self-reference: keeps the handler alive until ActivateCompleted runs, even
    // if we give up waiting below (timeout/stop) and release our own reference.
    handler->AddRef();
    IActivateAudioInterfaceAsyncOperation* async_op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &pv, handler, &async_op);
    if (FAILED(hr)) {
        LOGE("[ProcessLoopback] ActivateAudioInterfaceAsync failed 0x{:08x} pid={}",
             static_cast<unsigned>(hr), pid_);
        handler->Release();  // self-ref (no callback will arrive)
        handler->Release();  // caller ref
        return;
    }

    // Wait in slices so Stop() (want_running_ = false) breaks out quickly
    // instead of blocking on the full 15s timeout.
    bool activated = false;
    for (int waited = 0; waited < 15000 && want_running_.load(); waited += 100) {
        if (handler->WaitActivate(100)) {
            activated = true;
            break;
        }
    }
    if (!want_running_.load() || !activated || FAILED(handler->activate_hr())) {
        if (want_running_.load()) {
            LOGE("[ProcessLoopback] activate/init failed 0x{:08x} pid={}",
                 static_cast<unsigned>(handler->activate_hr()), pid_);
        } else {
            LOGI("[ProcessLoopback] activate cancelled by stop pid={}", pid_);
        }
        handler->Cancel();
        if (async_op) {
            async_op->Release();
        }
        handler->Release();  // caller ref; the self-ref goes with ActivateCompleted
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
    // Bytes per frame come from the negotiated client format, never assumed.
    const int frame_bytes = static_cast<int>(fmt.nBlockAlign);

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
    uint64_t transient_errors = 0;
    HRESULT loop_hr = S_OK;
    while (want_running_.load()) {
        WaitForSingleObject(handler->sample_event(), 50);
        UINT32 packet = 0;
        HRESULT hr_size = cap->GetNextPacketSize(&packet);
        while (SUCCEEDED(hr_size) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            const HRESULT hr_buf = cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr_buf)) {
                hr_size = hr_buf;
                break;
            }
            if (frames > 0 && data_callback_) {
                const int bytes = static_cast<int>(frames) * frame_bytes;
                if (!data || (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                    // Keep the sample clock continuous (OBS style): push an
                    // equal-length zero buffer instead of dropping the packet.
                    auto silent = Data::Make(nullptr, bytes);
                    if (silent && silent->DataAddr()) {
                        memset(silent->DataAddr(), 0, bytes);
                        data_callback_(silent);
                    }
                } else {
                    data_callback_(Data::Make(reinterpret_cast<const char*>(data), bytes));
                }
                ++packets;
                if (packets == 1 || (packets % 200) == 0) {
                    LOGI("[ProcessLoopback] packets={} last_bytes={}", packets, bytes);
                }
            }
            cap->ReleaseBuffer(frames);
            hr_size = cap->GetNextPacketSize(&packet);
        }
        if (FAILED(hr_size)) {
            if (IsFatalCaptureError(hr_size)) {
                loop_hr = hr_size;
                break;
            }
            if (++transient_errors == 1 || (transient_errors % 100) == 0) {
                LOGW("[ProcessLoopback] transient capture error 0x{:08x} n={} pid={}",
                     static_cast<unsigned>(hr_size), transient_errors, pid_);
            }
        }
    }

    if (FAILED(loop_hr)) {
        // Fatal device error: leave the thread instead of spinning forever, and
        // surface the stop so the plugin layer can schedule an auto-restart.
        LOGE("[ProcessLoopback] fatal capture error 0x{:08x} pid={}, capture thread exits",
             static_cast<unsigned>(loop_hr), pid_);
        fatal_stop_ = true;
        NotifyStopOnce();
    }

    client->Stop();
    running_ = false;
    handler->Release();
    LOGI("[ProcessLoopback] stopped pid={} packets={}", pid_, packets);
}

}  // namespace px
