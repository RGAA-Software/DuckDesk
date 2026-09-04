#include "process_loopback_audio_capture.h"
#include "px_common_new/async_runtime.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <wrl.h>
#include <wrl/client.h>
#include <wrl/implements.h>
#include <initguid.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <string>
#include <utility>

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

struct WinHandleCloser final {
    void operator()(void* handle) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): Win32 HANDLE boundary
        if (handle) {
            CloseHandle(handle);
        }
    }
};

using UniqueWinHandle = std::unique_ptr<void, WinHandleCloser>;
using Microsoft::WRL::ClassicCom;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::RuntimeClass;
using Microsoft::WRL::RuntimeClassFlags;

class ActivateHandler final : public RuntimeClass<
                                  RuntimeClassFlags<ClassicCom>,
                                  IActivateAudioInterfaceCompletionHandler,
                                  IAgileObject> {
public:
    ActivateHandler()
        : activate_done_(CreateEventW(nullptr, TRUE, FALSE, nullptr)),
          sample_ready_(CreateEventW(nullptr, FALSE, FALSE, nullptr)) {}

    [[nodiscard]] bool Ready() const {
        return activate_done_ && sample_ready_;
    }

    // NOLINTNEXTLINE(gammaray-raw-pointer-boundary): COM completion ABI
    HRESULT STDMETHODCALLTYPE ActivateCompleted(
        IActivateAudioInterfaceAsyncOperation* op) override {
        if (cancelled_.load()) {
            return S_OK;
        }

        activate_hr_ = E_FAIL;
        HRESULT hr_activate = E_FAIL;
        ComPtr<IUnknown> unknown;
        if (!op) {
            SetEvent(activate_done_.get());
            return S_OK;
        }
        op->GetActivateResult(&hr_activate, unknown.GetAddressOf());
        if (FAILED(hr_activate) || !unknown) {
            activate_hr_ = FAILED(hr_activate) ? hr_activate : E_FAIL;
            SetEvent(activate_done_.get());
            return S_OK;
        }

        HRESULT hr = unknown.As(&client_);
        if (FAILED(hr) || !client_) {
            activate_hr_ = hr;
            SetEvent(activate_done_.get());
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
            SetEvent(activate_done_.get());
            return S_OK;
        }

        // With AUTOCONVERTPCM, GetBuffer delivers frames already converted to the
        // requested client format above; log the engine mix format for diagnosis.
        WAVEFORMATEX* mix_value = nullptr; // NOLINT(gammaray-raw-pointer-boundary): COM allocated output boundary
        hr = client_->GetMixFormat(&mix_value);
        const std::unique_ptr<WAVEFORMATEX, decltype(&CoTaskMemFree)> mix(
            mix_value, &CoTaskMemFree);
        if (SUCCEEDED(hr) && mix) {
            LOGI("[ProcessLoopback] mix format: tag={} {}Hz {}ch {}bit align={}",
                 mix->wFormatTag, mix->nSamplesPerSec, mix->nChannels,
                 mix->wBitsPerSample, mix->nBlockAlign);
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
            SetEvent(activate_done_.get());
            return S_OK;
        }

        hr = client_->GetService(
            __uuidof(IAudioCaptureClient),
            reinterpret_cast<void**>(cap_.ReleaseAndGetAddressOf()));
        if (FAILED(hr) || !cap_) {
            activate_hr_ = FAILED(hr) ? hr : E_FAIL;
            SetEvent(activate_done_.get());
            return S_OK;
        }

        hr = client_->SetEventHandle(sample_ready_.get());
        if (FAILED(hr)) {
            activate_hr_ = hr;
            SetEvent(activate_done_.get());
            return S_OK;
        }

        activate_hr_ = S_OK;
        SetEvent(activate_done_.get());
        return S_OK;
    }

    bool WaitActivate(DWORD ms) {
        return WaitForSingleObject(activate_done_.get(), ms) == WAIT_OBJECT_0;
    }
    bool WaitSample(DWORD ms) {
        return WaitForSingleObject(sample_ready_.get(), ms) == WAIT_OBJECT_0;
    }
    // Marks the wait as given up: a late ActivateCompleted must not touch state.
    void Cancel() {
        cancelled_ = true;
        SetEvent(activate_done_.get());
    }
    HRESULT activate_hr() const { return activate_hr_; }
    [[nodiscard]] ComPtr<IAudioClient> client() const { return client_; }
    [[nodiscard]] ComPtr<IAudioCaptureClient> capture() const { return cap_; }
    const WAVEFORMATEX& format() const { return format_; }

private:
    std::atomic<bool> cancelled_{false};
    UniqueWinHandle activate_done_;
    UniqueWinHandle sample_ready_;
    HRESULT activate_hr_ = E_FAIL;
    ComPtr<IAudioClient> client_;
    ComPtr<IAudioCaptureClient> cap_;
    WAVEFORMATEX format_{};
};

}  // namespace

struct ProcessLoopbackAudioCapture::State final {
    State(
        uint32_t process_id,
        std::shared_ptr<IAudioCapture::CallbackState> callback_channel)
        : pid(process_id), callbacks(std::move(callback_channel)) {}

    const uint32_t pid;
    const std::shared_ptr<IAudioCapture::CallbackState> callbacks;
    std::mutex mutex;
    std::atomic_bool want_running = false;
    std::atomic_bool running = false;
    std::atomic_bool stop_notified = false;
    std::atomic_bool fatal_stop = false;
    std::jthread worker;
    std::atomic_int samples = 48000;
    std::atomic_int channels = 2;
    std::atomic_int bits = 16;
};

AudioCapturePtr ProcessLoopbackAudioCapture::Make(uint32_t process_id) {
    return std::make_shared<ProcessLoopbackAudioCapture>(process_id);
}

ProcessLoopbackAudioCapture::ProcessLoopbackAudioCapture(uint32_t process_id)
    : state_(std::make_shared<State>(process_id, callback_state_)) {}

ProcessLoopbackAudioCapture::~ProcessLoopbackAudioCapture() {
    Stop();
}

int ProcessLoopbackAudioCapture::Start() {
    const auto state = state_;
    if (state->pid == 0) {
        LOGE("[ProcessLoopback] Start failed: pid=0");
        return -1;
    }
    {
        std::lock_guard lock(state->mutex);
        if (state->running.load() || state->worker.joinable()) {
            // Idempotent: already running, or the worker is still starting up.
            LOGI("[ProcessLoopback] Start ignored: {} pid={}",
                 state->running.load() ? "already running" : "already starting",
                 state->pid);
            return 0;
        }
        state->want_running = true;
        state->stop_notified = false;
        state->fatal_stop = false;
        state->worker = std::jthread(
            [state](std::stop_token stop_token) {
                CaptureThreadMain(state, stop_token);
            });
    }
    // Wait for activate success/fail so caller can trust IsProviding().
    for (int i = 0; i < 200 && state->want_running.load(); ++i) {
        if (state->running.load()) {
            return 0;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    if (!state->running.load()) {
        Stop();
        LOGE("[ProcessLoopback] Start failed/timeout pid={}", state->pid);
        return -2;
    }
    return 0;
}

int ProcessLoopbackAudioCapture::Pause() {
    return Stop();
}

int ProcessLoopbackAudioCapture::Stop() {
    const auto state = state_;
    state->want_running = false;
    std::jthread joined;
    {
        std::lock_guard lock(state->mutex);
        if (state->worker.joinable()) {
            joined = std::move(state->worker);
        }
    }
    if (joined.joinable()) {
        joined.request_stop();
        if (joined.get_id() == std::this_thread::get_id()) {
            PxAsyncRuntime::DeferJoin(std::move(joined));
        } else {
            joined.join();
        }
    }
    state->running = false;
    NotifyStopOnce(state);
    return 0;
}

bool ProcessLoopbackAudioCapture::IsFatalStop() const {
    return state_->fatal_stop.load();
}

void ProcessLoopbackAudioCapture::NotifyStopOnce(
    const std::shared_ptr<State>& state) {
    const auto callback = SnapshotCallbacks(state->callbacks).stop;
    if (!state->stop_notified.exchange(true) && callback) {
        callback();
    }
}

void ProcessLoopbackAudioCapture::CaptureThreadMain(
    const std::shared_ptr<State>& state, std::stop_token stop_token) {
    const CoInitGuard co_init;
    if (!co_init.ok()) {
        LOGE("[ProcessLoopback] CoInitializeEx failed 0x{:08x}",
             static_cast<unsigned>(co_init.hr()));
        return;
    }

    AUDIOCLIENT_ACTIVATION_PARAMS ap{};
    ap.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
    ap.ProcessLoopbackParams.TargetProcessId = state->pid;
    ap.ProcessLoopbackParams.ProcessLoopbackMode =
        PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;

    PROPVARIANT pv{};
    PropVariantInit(&pv);
    pv.vt = VT_BLOB;
    pv.blob.cbSize = sizeof(ap);
    pv.blob.pBlobData = reinterpret_cast<BYTE*>(&ap);

    const auto handler = Microsoft::WRL::Make<ActivateHandler>();
    if (!handler || !handler->Ready()) {
        LOGE("[ProcessLoopback] failed to create activation events pid={}",
             state->pid);
        state->want_running = false;
        return;
    }
    ComPtr<IActivateAudioInterfaceAsyncOperation> async_operation;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &pv,
                                             handler.Get(),
                                             async_operation.GetAddressOf());
    if (FAILED(hr)) {
        LOGE("[ProcessLoopback] ActivateAudioInterfaceAsync failed 0x{:08x} pid={}",
             static_cast<unsigned>(hr), state->pid);
        state->want_running = false;
        return;
    }

    // Wait in slices so Stop() (want_running_ = false) breaks out quickly
    // instead of blocking on the full 15s timeout.
    bool activated = false;
    for (int waited = 0;
         waited < 15000 && state->want_running.load() &&
         !stop_token.stop_requested();
         waited += 100) {
        if (handler->WaitActivate(100)) {
            activated = true;
            break;
        }
    }
    if (!state->want_running.load() || stop_token.stop_requested() ||
        !activated || FAILED(handler->activate_hr())) {
        if (state->want_running.load() && !stop_token.stop_requested()) {
            LOGE("[ProcessLoopback] activate/init failed 0x{:08x} pid={}",
                 static_cast<unsigned>(handler->activate_hr()), state->pid);
        } else {
            LOGI("[ProcessLoopback] activate cancelled by stop pid={}", state->pid);
        }
        handler->Cancel();
        state->want_running = false;
        return;
    }

    const auto client = handler->client();
    const auto capture_client = handler->capture();
    const WAVEFORMATEX& fmt = handler->format();
    state->samples = static_cast<int>(fmt.nSamplesPerSec);
    state->channels = static_cast<int>(fmt.nChannels);
    state->bits = static_cast<int>(fmt.wBitsPerSample);
    // Bytes per frame come from the negotiated client format, never assumed.
    const int frame_bytes = static_cast<int>(fmt.nBlockAlign);

    const auto format_callback = SnapshotCallbacks(state->callbacks).format;
    if (format_callback) {
        format_callback(
            state->samples.load(), state->channels.load(), state->bits.load());
    }

    hr = client->Start();
    if (FAILED(hr)) {
        LOGE("[ProcessLoopback] client->Start failed 0x{:08x}", static_cast<unsigned>(hr));
        state->want_running = false;
        return;
    }

    state->running = true;
    LOGI("[ProcessLoopback] capturing pid={} {}Hz {}ch {}bit",
         state->pid, state->samples.load(), state->channels.load(),
         state->bits.load());

    uint64_t packets = 0;
    uint64_t transient_errors = 0;
    HRESULT loop_hr = S_OK;
    while (state->want_running.load() && !stop_token.stop_requested()) {
        handler->WaitSample(50);
        UINT32 packet = 0;
        HRESULT hr_size = capture_client->GetNextPacketSize(&packet);
        while (SUCCEEDED(hr_size) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            const HRESULT hr_buf = capture_client->GetBuffer(
                &data, &frames, &flags, nullptr, nullptr);
            if (FAILED(hr_buf)) {
                hr_size = hr_buf;
                break;
            }
            const auto data_callback = SnapshotCallbacks(state->callbacks).data;
            if (frames > 0 && data_callback) {
                const int bytes = static_cast<int>(frames) * frame_bytes;
                if (!data || (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0) {
                    // Keep the sample clock continuous (OBS style): push an
                    // equal-length zero buffer instead of dropping the packet.
                    auto silent = Data::Make(nullptr, bytes);
                    if (silent && silent->DataAddr()) {
                        memset(silent->DataAddr(), 0, bytes);
                        data_callback(silent);
                    }
                } else {
                    data_callback(Data::Make(
                        reinterpret_cast<const char*>(data), bytes));
                }
                ++packets;
                if (packets == 1 || (packets % 200) == 0) {
                    LOGI("[ProcessLoopback] packets={} last_bytes={}", packets, bytes);
                }
            }
            capture_client->ReleaseBuffer(frames);
            hr_size = capture_client->GetNextPacketSize(&packet);
        }
        if (FAILED(hr_size)) {
            if (IsFatalCaptureError(hr_size)) {
                loop_hr = hr_size;
                break;
            }
            if (++transient_errors == 1 || (transient_errors % 100) == 0) {
                LOGW("[ProcessLoopback] transient capture error 0x{:08x} n={} pid={}",
                     static_cast<unsigned>(hr_size), transient_errors, state->pid);
            }
        }
    }

    if (FAILED(loop_hr)) {
        // Fatal device error: leave the thread instead of spinning forever, and
        // surface the stop so the plugin layer can schedule an auto-restart.
        LOGE("[ProcessLoopback] fatal capture error 0x{:08x} pid={}, capture thread exits",
             static_cast<unsigned>(loop_hr), state->pid);
        state->fatal_stop = true;
        NotifyStopOnce(state);
    }

    client->Stop();
    state->running = false;
    state->want_running = false;
    LOGI("[ProcessLoopback] stopped pid={} packets={}", state->pid, packets);
}

}  // namespace px
