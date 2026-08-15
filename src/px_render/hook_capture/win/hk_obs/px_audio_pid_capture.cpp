// Helper: PID process-loopback capture → WAV (external process).
// Self-loopback from inside the game process yields silence; this helper is
// started by the injected hook DLL / test harness instead.
//
// Usage:
//   px_audio_pid_capture.exe <pid> <out.wav> [seconds]
// seconds=0 → run until target process exits (1h cap).

#include <Windows.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace {

#pragma pack(push, 1)
struct WavHdr {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t chunk_size = 36;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t audio_format = 1;
    uint16_t channels = 2;
    uint32_t sample_rate = 44100;
    uint32_t byte_rate = 0;
    uint16_t block_align = 0;
    uint16_t bits = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0;
};
#pragma pack(pop)

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
        format_.nBlockAlign = 4;
        format_.nAvgBytesPerSec = 44100 * 4;
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
        activate_hr_ = hr;
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

bool ProcessAlive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

void FlushHeader(FILE* f, const WavHdr& proto, uint32_t data_bytes) {
    WavHdr h = proto;
    h.data_size = data_bytes;
    h.chunk_size = 36 + data_bytes;
    const long pos = ftell(f);
    fseek(f, 0, SEEK_SET);
    fwrite(&h, sizeof(h), 1, f);
    fflush(f);
    if (pos >= 0) {
        fseek(f, pos, SEEK_SET);
    }
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) {
        fwprintf(stderr, L"Usage: %s <pid> <out.wav> [seconds]\n", argv[0]);
        return 1;
    }
    const uint32_t pid = static_cast<uint32_t>(_wtoi(argv[1]));
    const std::filesystem::path out = argv[2];
    const int seconds = (argc >= 4) ? _wtoi(argv[3]) : 0;
    if (pid == 0) {
        return 2;
    }

    // Caller is expected to warm up the game; short settle only.
    Sleep(500);

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        return 3;
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
        handler->Release();
        return 4;
    }
    if (!handler->WaitActivate(15000) || FAILED(handler->activate_hr())) {
        if (async_op) {
            async_op->Release();
        }
        handler->Release();
        return 5;
    }
    if (async_op) {
        async_op->Release();
    }

    IAudioClient* client = handler->client();
    IAudioCaptureClient* cap = handler->capture();
    const WAVEFORMATEX& fmt = handler->format();
    if (FAILED(client->Start())) {
        handler->Release();
        return 6;
    }

    std::error_code ec;
    std::filesystem::create_directories(out.parent_path(), ec);
    FILE* f = _wfopen(out.c_str(), L"wb");
    if (!f) {
        handler->Release();
        return 7;
    }

    WavHdr hdr{};
    hdr.channels = static_cast<uint16_t>(fmt.nChannels);
    hdr.sample_rate = fmt.nSamplesPerSec;
    hdr.bits = 16;
    hdr.block_align = static_cast<uint16_t>(fmt.nChannels * 2);
    hdr.byte_rate = hdr.sample_rate * hdr.block_align;
    fwrite(&hdr, sizeof(hdr), 1, f);
    fflush(f);

    uint32_t data_bytes = 0;
    uint32_t last_flush = 0;
    const auto start = std::chrono::steady_clock::now();
    const auto limit = (seconds > 0) ? start + std::chrono::seconds(seconds)
                                     : start + std::chrono::hours(1);

    while (std::chrono::steady_clock::now() < limit && ProcessAlive(pid)) {
        WaitForSingleObject(handler->sample_event(), 50);
        UINT32 packet = 0;
        while (SUCCEEDED(cap->GetNextPacketSize(&packet)) && packet > 0) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(cap->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                break;
            }
            if (frames > 0 && data && (flags & AUDCLNT_BUFFERFLAGS_SILENT) == 0) {
                const size_t nbytes = static_cast<size_t>(frames) * fmt.nBlockAlign;
                fwrite(data, 1, nbytes, f);
                data_bytes += static_cast<uint32_t>(nbytes);
                if (data_bytes - last_flush >= 44100 * 4) {
                    FlushHeader(f, hdr, data_bytes);
                    last_flush = data_bytes;
                }
            }
            cap->ReleaseBuffer(frames);
        }
    }

    client->Stop();
    FlushHeader(f, hdr, data_bytes);
    fclose(f);
    handler->Release();
    return data_bytes < fmt.nAvgBytesPerSec ? 8 : 0;
}
