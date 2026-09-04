// Diagnose MiniAudio PID process-loopback vs raw WASAPI.
// Usage:
//   test_miniaudio_pid_loopback.exe <pid>
//
// Shows why MiniAudio fails on Win32 desktop (IMMDevice::GetDevice on
// VAD\Process_Loopback) and whether our patched MiniAudio path works.

#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "miniaudio_audio_capture.h"
#include "third_party/miniaudio/miniaudio.h"

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace {

void ProbeImmDevice(const wchar_t* id) {
    IMMDeviceEnumerator* en = nullptr;
    HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                  __uuidof(IMMDeviceEnumerator), (void**)&en);
    std::printf("[1] CoCreate MMDeviceEnumerator hr=0x%08lx\n", (unsigned long)hr);
    if (FAILED(hr) || !en) {
        return;
    }
    IMMDevice* dev = nullptr;
    hr = en->GetDevice(id, &dev);
    std::printf("[1] IMMDevice::GetDevice(\"%ls\") hr=0x%08lx (%s)\n", id, (unsigned long)hr,
                FAILED(hr) ? "FAIL — this is what MiniAudio desktop does for PID loopback"
                           : "ok");
    if (dev) {
        dev->Release();
    }
    en->Release();
}

class Handler final : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    Handler() : ev_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    ~Handler() {
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
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation* op) override {
        HRESULT act = E_FAIL;
        IUnknown* unk = nullptr;
        if (op) {
            op->GetActivateResult(&act, &unk);
        }
        hr_ = act;
        if (unk) {
            unk->Release();
        }
        SetEvent(ev_);
        return S_OK;
    }
    bool Wait(DWORD ms) { return WaitForSingleObject(ev_, ms) == WAIT_OBJECT_0; }
    HRESULT hr() const { return hr_; }

private:
    std::atomic<ULONG> ref_{1};
    HANDLE ev_ = nullptr;
    HRESULT hr_ = E_FAIL;
};

void ProbeActivateAsync(uint32_t pid) {
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

    auto* h = new Handler();
    IActivateAudioInterfaceAsyncOperation* op = nullptr;
    HRESULT hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK,
                                             __uuidof(IAudioClient), &pv, h, &op);
    std::printf("[2] ActivateAudioInterfaceAsync hr=0x%08lx\n", (unsigned long)hr);
    if (SUCCEEDED(hr)) {
        const bool ok = h->Wait(10000);
        std::printf("[2] activate completed=%d activate_hr=0x%08lx (%s)\n", ok ? 1 : 0,
                    (unsigned long)h->hr(),
                    SUCCEEDED(h->hr()) ? "OK — correct PID loopback API"
                                       : "FAIL");
    }
    if (op) {
        op->Release();
    }
    h->Release();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("Usage: %s <pid>\n", argv[0]);
        return 1;
    }
    const uint32_t pid = (uint32_t)atoi(argv[1]);
    std::printf("Target pid=%u  OS build check via MiniAudio path\n", pid);

    HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    std::printf("CoInitializeEx=0x%08lx\n", (unsigned long)co);

    std::printf("\n=== A) What MiniAudio desktop does (broken) ===\n");
    ProbeImmDevice(L"VAD\\Process_Loopback");

    std::printf("\n=== B) What our WASAPI path / MS sample does (works) ===\n");
    ProbeActivateAsync(pid);

    std::printf("\n=== C) MiniAudioCapture::MakeForProcess ===\n");
    std::atomic<uint64_t> bytes{0};
    std::atomic<int> peak{0};
    auto cap = px::MiniAudioCapture::MakeForProcess(pid);
    cap->RegisterFormatCallback(
        [](int sr, int ch, int bits) { std::printf("[3] format %dHz %dch %dbit\n", sr, ch, bits); });
    cap->RegisterDataCallback([&](const px::DataPtr& d) {
        if (!d) {
            return;
        }
        bytes += (uint64_t)d->Size();
        const auto* s = reinterpret_cast<const int16_t*>(d->DataAddr());
        const size_t n = d->Size() / sizeof(int16_t);
        for (size_t i = 0; i < n; ++i) {
            const int a = s[i] < 0 ? -s[i] : s[i];
            int prev = peak.load();
            while (a > prev && !peak.compare_exchange_weak(prev, a)) {
            }
        }
    });
    const int start = cap->Start();
    std::printf("[3] MiniAudio Start() => %d (%s)\n", start,
                start == 0 ? "OK" : "FAIL");
    if (start == 0) {
        std::this_thread::sleep_for(std::chrono::seconds(3));
        cap->Stop();
        std::printf("[3] captured bytes=%llu peak=%d\n", (unsigned long long)bytes.load(),
                    peak.load());
        std::printf("[3] RESULT: MiniAudio PID loopback WORKS\n");
        return 0;
    }
    std::printf("[3] RESULT: MiniAudio PID loopback STILL FAILS (see A)\n");
    return 2;
}
