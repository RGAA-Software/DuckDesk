// PID process-loopback capture only (no device-mix fallback).
// Matches Microsoft ApplicationLoopback sample:
//   ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK)
//   Initialize inside ActivateCompleted with PCM16 44100 + LOOPBACK|EVENTCALLBACK|AUTOCONVERTPCM
//
// Usage:
//   test_game_audio_loopback.exe [seconds] [optional_exe_path]

#include <Windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <audioclientactivationparams.h>
#include <mmdeviceapi.h>
#include <mmreg.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "mmdevapi.lib")

namespace {

constexpr wchar_t kDefaultGame[] =
    L"D:\\1_test_games\\CarGame  汽车\\CarGame\\Binaries\\Win64\\VehicleGame-Win64-Shipping.exe";

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

void WriteWav(const std::filesystem::path& path,
              const std::vector<int16_t>& pcm,
              int channels,
              int rate) {
    WavHdr h{};
    h.channels = static_cast<uint16_t>(channels);
    h.sample_rate = static_cast<uint32_t>(rate);
    h.bits = 16;
    h.block_align = static_cast<uint16_t>(channels * 2);
    h.byte_rate = h.sample_rate * h.block_align;
    h.data_size = static_cast<uint32_t>(pcm.size() * sizeof(int16_t));
    h.chunk_size = 36 + h.data_size;
    FILE* f = _wfopen(path.c_str(), L"wb");
    if (!f) {
        std::wcerr << L"open failed: " << path.wstring() << L"\n";
        return;
    }
    fwrite(&h, sizeof(h), 1, f);
    fwrite(pcm.data(), sizeof(int16_t), pcm.size(), f);
    fclose(f);
}

uint32_t StartGame(const std::wstring& path) {
    std::wstring work = std::filesystem::path(path).parent_path().wstring();
    std::wstring cmd = L"\"" + path + L"\"";
    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(path.c_str(), buf.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        work.c_str(), &si, &pi)) {
        std::cerr << "CreateProcess failed: " << GetLastError() << "\n";
        return 0;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    std::wcout << L"Started pid=" << pi.dwProcessId << L"\n";
    return pi.dwProcessId;
}

// Activation + Initialize must complete on the MTA callback thread (MS sample).
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

        // Same pattern as CingZeoi/AudioLoopbackRecorder + MS ApplicationLoopback:
        // Initialize inside ActivateCompleted; PCM16/44100; LOOPBACK|EVENTCALLBACK|AUTOCONVERTPCM.
        // Note: AUTOCONVERTPCM must be in StreamFlags (not hnsPeriodicity).
        const DWORD flags = AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_EVENTCALLBACK |
                            AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM;
        hr = client_->Initialize(AUDCLNT_SHAREMODE_SHARED, flags,
                                 /*hnsBufferDuration*/ 200000, /*hnsPeriodicity*/ 0, &format_,
                                 nullptr);
        if (FAILED(hr)) {
            std::cerr << "Initialize(PID loopback) failed: 0x" << std::hex << hr << std::dec
                      << "\n";
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

int wmain(int argc, wchar_t** argv) {
    int seconds = 15;
    if (argc >= 2) {
        seconds = std::max(5, _wtoi(argv[1]));
    }
    std::wstring game = kDefaultGame;
    if (argc >= 3) {
        game = argv[2];
    }

    system("taskkill /F /IM VehicleGame-Win64-Shipping.exe >nul 2>nul");
    std::this_thread::sleep_for(std::chrono::seconds(1));

    const uint32_t pid = StartGame(game);
    if (!pid) {
        return 1;
    }

    const int warmup_s = (game.find(L"PlayTone") != std::wstring::npos) ? 1 : 15;
    std::cout << "Warmup " << warmup_s << "s (PID process-loopback only, no device mix)...\n";
    for (int i = 0; i < warmup_s; i++) {
        EnumWindows(
            [](HWND w, LPARAM lp) -> BOOL {
                DWORD wpid = 0;
                GetWindowThreadProcessId(w, &wpid);
                if (wpid == static_cast<DWORD>(lp) && IsWindowVisible(w)) {
                    SetForegroundWindow(w);
                    return FALSE;
                }
                return TRUE;
            },
            static_cast<LPARAM>(pid));
        keybd_event(VK_RETURN, 0, 0, 0);
        keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(hr) && hr != S_FALSE && hr != RPC_E_CHANGED_MODE) {
        std::cerr << "CoInitializeEx failed\n";
        return 2;
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
        std::cerr << "ActivateAudioInterfaceAsync failed: 0x" << std::hex << hr << std::dec
                  << "\n";
        handler->Release();
        return 3;
    }
    if (!handler->WaitActivate(15000) || FAILED(handler->activate_hr())) {
        std::cerr << "PID process-loopback activate/init failed: 0x" << std::hex
                  << handler->activate_hr() << std::dec << "\n";
        if (async_op) {
            async_op->Release();
        }
        handler->Release();
        return 4;
    }
    if (async_op) {
        async_op->Release();
    }

    IAudioClient* client = handler->client();
    IAudioCaptureClient* cap = handler->capture();
    const WAVEFORMATEX& fmt = handler->format();
    std::cout << "PID process-loopback OK pid=" << pid << " format=" << fmt.nSamplesPerSec
              << "Hz " << fmt.nChannels << "ch " << fmt.wBitsPerSample << "bit\n";

    hr = client->Start();
    if (FAILED(hr)) {
        std::cerr << "Start failed: 0x" << std::hex << hr << std::dec << "\n";
        handler->Release();
        return 5;
    }

    std::vector<int16_t> pcm;
    pcm.reserve(static_cast<size_t>(fmt.nSamplesPerSec) * fmt.nChannels * seconds);
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    std::cout << "Recording " << seconds << "s via PID process-loopback...\n";

    while (std::chrono::steady_clock::now() < end) {
        EnumWindows(
            [](HWND w, LPARAM lp) -> BOOL {
                DWORD wpid = 0;
                GetWindowThreadProcessId(w, &wpid);
                if (wpid == static_cast<DWORD>(lp) && IsWindowVisible(w)) {
                    SetForegroundWindow(w);
                    return FALSE;
                }
                return TRUE;
            },
            static_cast<LPARAM>(pid));
        keybd_event(VK_RETURN, 0, 0, 0);
        keybd_event(VK_RETURN, 0, KEYEVENTF_KEYUP, 0);
        keybd_event(VK_SPACE, 0, 0, 0);
        keybd_event(VK_SPACE, 0, KEYEVENTF_KEYUP, 0);

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
                const auto* s = reinterpret_cast<const int16_t*>(data);
                const size_t n = static_cast<size_t>(frames) * fmt.nChannels;
                pcm.insert(pcm.end(), s, s + n);
            }
            cap->ReleaseBuffer(frames);
        }
    }

    client->Stop();
    const auto out = std::filesystem::path(L"C:\\Users\\Public\\Pixels\\hook_audio_game_pid.wav");
    WriteWav(out, pcm, fmt.nChannels, static_cast<int>(fmt.nSamplesPerSec));
    std::wcout << L"Wrote " << out.wstring() << L" samples=" << pcm.size() << L" (~"
               << (fmt.nChannels ? pcm.size() / (fmt.nChannels * fmt.nSamplesPerSec) : 0)
               << L"s)\n";

    // Require real energy (all-zero PCM still has sample count).
    double sum_sq = 0;
    for (int16_t s : pcm) {
        const double v = s / 32768.0;
        sum_sq += v * v;
    }
    const double rms =
        pcm.empty() ? 0.0 : std::sqrt(sum_sq / static_cast<double>(pcm.size()));
    const double dbfs = (rms < 1e-9) ? -91.0 : (20.0 * std::log10(rms));
    std::cout << "mean_dbfs=" << dbfs << "\n";

    handler->Release();
    system("taskkill /F /IM VehicleGame-Win64-Shipping.exe >nul 2>nul");

    if (pcm.size() < static_cast<size_t>(fmt.nSamplesPerSec)) {
        std::cerr << "FAIL: too little PID-loopback audio\n";
        return 6;
    }
    if (dbfs < -55.0) {
        std::cerr << "FAIL: PID-loopback too quiet (silence)\n";
        return 7;
    }
    std::cout << "PASS (PID process-loopback / 内录)\n";
    return 0;
}
