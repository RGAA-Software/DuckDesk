#include "HookXAudio2.h"

#include <Windows.h>
#include <detours/detours.h>
#include <mmreg.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "AudioHookCommon.h"
#include "tc_common_new/log.h"

namespace tc {
namespace {

struct Vtable {
    void* func[64];
};
struct ComObj {
    Vtable* vtbl;
};

using XAudio2CreateFn = HRESULT(WINAPI*)(IXAudio2**, UINT32, XAUDIO2_PROCESSOR);
using CreateSourceVoiceFn = HRESULT(STDMETHODCALLTYPE*)(IXAudio2*,
                                                        IXAudio2SourceVoice**,
                                                        const WAVEFORMATEX*,
                                                        UINT32,
                                                        float,
                                                        IXAudio2VoiceCallback*,
                                                        const XAUDIO2_VOICE_SENDS*,
                                                        const XAUDIO2_EFFECT_CHAIN*);
using SubmitSourceBufferFn = HRESULT(STDMETHODCALLTYPE*)(IXAudio2SourceVoice*,
                                                         const XAUDIO2_BUFFER*,
                                                         const XAUDIO2_BUFFER_WMA*);

constexpr int kIxAudio2CreateSourceVoice = 5;
constexpr int kSourceVoiceSubmitBuffer = 21;

XAudio2CreateFn origin_XAudio2Create = nullptr;

std::mutex g_voice_mu;
std::unordered_map<IXAudio2SourceVoice*, WAVEFORMATEX> g_voice_fmt;
std::unordered_map<void*, CreateSourceVoiceFn> g_orig_create_voice;
std::unordered_map<void*, SubmitSourceBufferFn> g_orig_submit;
std::atomic<uint64_t> g_submit_calls{0};
std::atomic<uint64_t> g_submit_posted{0};
std::atomic<uint64_t> g_voices{0};

bool AttachExport(void** target, void* detour) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (DetourAttach(target, detour) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
}

bool PatchSlot(void** slot, void* detour) {
    if (!slot || !detour || !*slot) {
        return false;
    }
    if (*slot == detour) {
        return true;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

SimpleAudioFormat FormatFromWave(const WAVEFORMATEX* fmt) {
    if (!fmt) {
        return SimpleAudioFormat::kPCM_S16;
    }
    if (fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (fmt->wBitsPerSample == 32 && fmt->wFormatTag != WAVE_FORMAT_PCM)) {
        return SimpleAudioFormat::kPCM_F32;
    }
    return SimpleAudioFormat::kPCM_S16;
}

bool IsPcmLike(const WAVEFORMATEX* fmt) {
    if (!fmt || fmt->nChannels == 0 || fmt->nSamplesPerSec == 0 || fmt->nBlockAlign == 0) {
        return false;
    }
    return fmt->wFormatTag == WAVE_FORMAT_PCM || fmt->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
           fmt->wFormatTag == WAVE_FORMAT_EXTENSIBLE;
}

HRESULT STDMETHODCALLTYPE Hook_SubmitSourceBuffer(IXAudio2SourceVoice* thiz,
                                                  const XAUDIO2_BUFFER* buffer,
                                                  const XAUDIO2_BUFFER_WMA* wma) {
    SubmitSourceBufferFn orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_voice_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_submit.find(obj->vtbl);
            if (it != g_orig_submit.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }

    g_submit_calls.fetch_add(1, std::memory_order_relaxed);
    float peak = 0.f;
    bool have_fmt = false;
    if (buffer && buffer->pAudioData && buffer->AudioBytes > 0 && !wma) {
        WAVEFORMATEX fmt{};
        {
            std::lock_guard lock(g_voice_mu);
            auto it = g_voice_fmt.find(thiz);
            if (it != g_voice_fmt.end()) {
                fmt = it->second;
                have_fmt = true;
            }
        }
        if (!have_fmt) {
            fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            fmt.nChannels = 2;
            fmt.nSamplesPerSec = 48000;
            fmt.wBitsPerSample = 32;
            fmt.nBlockAlign = 8;
            fmt.nAvgBytesPerSec = 48000 * 8;
        }
        if (IsPcmLike(&fmt)) {
            const auto sfmt = FormatFromWave(&fmt);
            UINT32 bytes = buffer->AudioBytes;
            const char* data = reinterpret_cast<const char*>(buffer->pAudioData);
            if (buffer->PlayBegin > 0 && fmt.nBlockAlign > 0) {
                const size_t skip =
                    static_cast<size_t>(buffer->PlayBegin) * fmt.nBlockAlign;
                if (skip < bytes) {
                    data += skip;
                    bytes -= static_cast<UINT32>(skip);
                }
            }
            if (buffer->PlayLength > 0 && fmt.nBlockAlign > 0) {
                const UINT32 play_bytes = buffer->PlayLength * fmt.nBlockAlign;
                if (play_bytes < bytes) {
                    bytes = play_bytes;
                }
            }
            if (bytes > 0) {
                peak = BufferPeak(data, static_cast<int>((std::min)(bytes, 2048u)), sfmt);
                if (peak > 1.0e-5f && !WasapiCaptureActive()) {
                    PushHookedPcm(data, static_cast<int>(bytes), sfmt,
                                  static_cast<int>(fmt.nSamplesPerSec),
                                  static_cast<int>(fmt.nChannels), "XAudio2");
                    g_submit_posted.fetch_add(1, std::memory_order_relaxed);
                }
            }
        }
    }

    const auto n = g_submit_calls.load(std::memory_order_relaxed);
    if (n == 1 || (n % 200) == 0) {
        LOGI("XAudio2 Submit: calls={} posted={} voices={} have_fmt={} wma={} "
             "peak={:.6f} bytes={}",
             n, g_submit_posted.load(std::memory_order_relaxed), g_voices.load(),
             have_fmt ? 1 : 0, wma ? 1 : 0, peak, buffer ? buffer->AudioBytes : 0);
    }
    return orig(thiz, buffer, wma);
}

void HookVoiceSubmit(IXAudio2SourceVoice* voice) {
    if (!voice) {
        return;
    }
    auto* obj = reinterpret_cast<ComObj*>(voice);
    if (!obj || !obj->vtbl) {
        return;
    }
    std::lock_guard lock(g_voice_mu);
    if (g_orig_submit.count(obj->vtbl)) {
        return;
    }
    auto* slot = &obj->vtbl->func[kSourceVoiceSubmitBuffer];
    if (!*slot || *slot == reinterpret_cast<void*>(&Hook_SubmitSourceBuffer)) {
        return;
    }
    g_orig_submit[obj->vtbl] = reinterpret_cast<SubmitSourceBufferFn>(*slot);
    PatchSlot(slot, reinterpret_cast<void*>(&Hook_SubmitSourceBuffer));
    LOGI("XAudio2 SubmitSourceBuffer hooked vtbl={}", static_cast<const void*>(obj->vtbl));
}

HRESULT STDMETHODCALLTYPE Hook_CreateSourceVoice(IXAudio2* thiz,
                                                 IXAudio2SourceVoice** pp,
                                                 const WAVEFORMATEX* format,
                                                 UINT32 flags,
                                                 float max_freq,
                                                 IXAudio2VoiceCallback* cb,
                                                 const XAUDIO2_VOICE_SENDS* sends,
                                                 const XAUDIO2_EFFECT_CHAIN* fx) {
    CreateSourceVoiceFn orig = nullptr;
    {
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        std::lock_guard lock(g_voice_mu);
        if (obj && obj->vtbl) {
            auto it = g_orig_create_voice.find(obj->vtbl);
            if (it != g_orig_create_voice.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto hr = orig(thiz, pp, format, flags, max_freq, cb, sends, fx);
    if (FAILED(hr) || !pp || !*pp || !format) {
        return hr;
    }
    {
        WAVEFORMATEX stored = *format;
        stored.cbSize = 0;
        std::lock_guard lock(g_voice_mu);
        g_voice_fmt[*pp] = stored;
    }
    HookVoiceSubmit(*pp);
    const auto n = g_voices.fetch_add(1) + 1;
    LOGI("XAudio2 CreateSourceVoice #{}: {}Hz {}ch {}bit tag=0x{:x}", n, format->nSamplesPerSec,
         format->nChannels, format->wBitsPerSample, format->wFormatTag);
    return hr;
}

bool PatchCreateSourceVoice(IXAudio2* engine) {
    if (!engine) {
        return false;
    }
    auto* obj = reinterpret_cast<ComObj*>(engine);
    if (!obj || !obj->vtbl) {
        return false;
    }
    std::lock_guard lock(g_voice_mu);
    if (g_orig_create_voice.count(obj->vtbl)) {
        return true;
    }
    auto* slot = &obj->vtbl->func[kIxAudio2CreateSourceVoice];
    if (!*slot || *slot == reinterpret_cast<void*>(&Hook_CreateSourceVoice)) {
        return false;
    }
    g_orig_create_voice[obj->vtbl] = reinterpret_cast<CreateSourceVoiceFn>(*slot);
    if (!PatchSlot(slot, reinterpret_cast<void*>(&Hook_CreateSourceVoice))) {
        g_orig_create_voice.erase(obj->vtbl);
        return false;
    }
    LOGI("XAudio2 CreateSourceVoice hooked engine={}", static_cast<const void*>(engine));
    return true;
}

HRESULT WINAPI Hook_XAudio2Create(IXAudio2** pp, UINT32 flags, XAUDIO2_PROCESSOR proc) {
    auto hr = origin_XAudio2Create(pp, flags, proc);
    if (SUCCEEDED(hr) && pp && *pp) {
        PatchCreateSourceVoice(*pp);
        LOGI("XAudio2Create ok engine={}", static_cast<const void*>(*pp));
    }
    return hr;
}

HMODULE FindXAudioModule() {
    const wchar_t* names[] = {L"xaudio2_9redist.dll", L"xaudio2_9.dll", L"XAudio2_9.dll",
                              L"XAudio2_8.dll", L"XAudio2_7.dll"};
    for (auto* n : names) {
        if (HMODULE m = GetModuleHandleW(n)) {
            return m;
        }
    }
    return nullptr;
}

}  // namespace

HookXAudio2* HookXAudio2::Instance() {
    static HookXAudio2 inst;
    return &inst;
}

bool HookXAudio2::Start(std::shared_ptr<AudioMixer> mixer) {
    mixer_ = std::move(mixer);
    stop_ = 0;
    installed_ = 0;
    if (TryInstall()) {
        return true;
    }
    HANDLE th = CreateThread(nullptr, 0,
                             [](LPVOID p) -> DWORD {
                                 reinterpret_cast<HookXAudio2*>(p)->WatcherMain();
                                 return 0;
                             },
                             this, 0, nullptr);
    watcher_ = th;
    LOGI("XAudio2: watcher started");
    return true;
}

void HookXAudio2::Stop() {
    stop_ = 1;
    if (watcher_) {
        WaitForSingleObject(static_cast<HANDLE>(watcher_), 3000);
        CloseHandle(static_cast<HANDLE>(watcher_));
        watcher_ = nullptr;
    }
}

void HookXAudio2::WatcherMain() {
    for (int i = 0; i < 6000 && !stop_; i++) {
        if (TryInstall()) {
            return;
        }
        Sleep(5);
    }
}

bool HookXAudio2::TryInstall() {
    if (installed_) {
        return true;
    }
    HMODULE mod = FindXAudioModule();
    if (!mod) {
        return false;
    }
    auto* create = reinterpret_cast<XAudio2CreateFn>(GetProcAddress(mod, "XAudio2Create"));
    if (!create) {
        LOGE("XAudio2: XAudio2Create export missing");
        return false;
    }
    // Patch shared CreateSourceVoice vtable via a temporary engine BEFORE
    // detouring the export, so engines created in a race still get hooked.
    IXAudio2* tmp = nullptr;
    if (SUCCEEDED(create(&tmp, 0, XAUDIO2_DEFAULT_PROCESSOR)) && tmp) {
        PatchCreateSourceVoice(tmp);
        tmp->Release();
    }
    origin_XAudio2Create = create;
    if (!AttachExport(reinterpret_cast<void**>(&origin_XAudio2Create),
                      reinterpret_cast<void*>(&Hook_XAudio2Create))) {
        LOGE("XAudio2: Detour XAudio2Create failed");
        return false;
    }
    installed_ = 1;
    LOGI("XAudio2 hooks ready (vtable + export)");
    return true;
}

}  // namespace tc
