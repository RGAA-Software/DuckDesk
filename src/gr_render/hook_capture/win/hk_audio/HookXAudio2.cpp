#include "HookXAudio2.h"

#include <Windows.h>
#include <detours/detours.h>
#include <mmreg.h>
#include <xaudio2.h>

#include <algorithm>
#include <atomic>
#include <cstring>
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
// Full extensible copy so SubFormat survives (cbSize=0 truncation misjudged
// 32-bit int PCM as float).
std::unordered_map<IXAudio2SourceVoice*, WAVEFORMATEXTENSIBLE> g_voice_fmt;
std::unordered_map<void*, CreateSourceVoiceFn> g_orig_create_voice;
std::unordered_map<void*, SubmitSourceBufferFn> g_orig_submit;
std::atomic<uint64_t> g_submit_calls{0};
std::atomic<uint64_t> g_submit_posted{0};
std::atomic<uint64_t> g_submit_no_fmt{0};
std::atomic<uint64_t> g_submit_chunked{0};
std::atomic<uint64_t> g_voices{0};

bool AttachExport(void** target, void* detour) {
    DetourTransactionBegin();
    DetourUpdateAllThreads();
    if (DetourAttach(target, detour) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
}

bool DetachExport(void** target, void* detour) {
    DetourTransactionBegin();
    DetourUpdateAllThreads();
    if (DetourDetach(target, detour) != NO_ERROR) {
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

// Undo PatchSlot: only writes back when the slot still points at our detour.
void RestoreSlot(void** slot, void* detour, void* orig) {
    if (!slot || !detour || !orig) {
        return;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return;
    }
    if (*slot == detour) {
        *slot = orig;
    }
    VirtualProtect(slot, sizeof(void*), old, &old);
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
        WAVEFORMATEXTENSIBLE fmt{};
        {
            std::lock_guard lock(g_voice_mu);
            auto it = g_voice_fmt.find(thiz);
            if (it != g_voice_fmt.end()) {
                fmt = it->second;
                have_fmt = true;
            }
        }
        SimpleAudioFormat sfmt = SimpleAudioFormat::kPCM_S16;
        // Unknown/unsupported format: drop instead of fabricating 48k/f32 and
        // reading the buffer with the wrong layout.
        if (!have_fmt || !ResolveWaveFormat(&fmt.Format, sfmt)) {
            const auto n = g_submit_no_fmt.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 200) == 0) {
                LOGW("XAudio2 Submit: drop buffer, unknown format have_fmt={} n={}",
                     have_fmt ? 1 : 0, n);
            }
        } else {
            UINT32 bytes = buffer->AudioBytes;
            const char* data = reinterpret_cast<const char*>(buffer->pAudioData);
            if (buffer->PlayBegin > 0 && fmt.Format.nBlockAlign > 0) {
                const size_t skip =
                    static_cast<size_t>(buffer->PlayBegin) * fmt.Format.nBlockAlign;
                if (skip < bytes) {
                    data += skip;
                    bytes -= static_cast<UINT32>(skip);
                }
            }
            if (buffer->PlayLength > 0 && fmt.Format.nBlockAlign > 0) {
                const UINT32 play_bytes = buffer->PlayLength * fmt.Format.nBlockAlign;
                if (play_bytes < bytes) {
                    bytes = play_bytes;
                }
            }
            // A single submit can carry tens of MB — push in ~1s chunks so the
            // mixer queue stays bounded and latency does not spike.
            const UINT32 chunk = static_cast<UINT32>(
                (std::max)(1ul, static_cast<unsigned long>(fmt.Format.nSamplesPerSec) *
                                    fmt.Format.nBlockAlign));
            if (bytes > chunk) {
                g_submit_chunked.fetch_add(1, std::memory_order_relaxed);
            }
            for (size_t off = 0; off < bytes; off += chunk) {
                const UINT32 n = static_cast<UINT32>(
                    (std::min)(static_cast<size_t>(chunk), bytes - off));
                peak = BufferPeak(data + off, static_cast<int>((std::min)(n, 2048u)), sfmt);
                if (peak > 1.0e-5f) {
                    PushHookedPcm(data + off, static_cast<int>(n), sfmt,
                                  static_cast<int>(fmt.Format.nSamplesPerSec),
                                  static_cast<int>(fmt.Format.nChannels), "XAudio2");
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
        WAVEFORMATEXTENSIBLE stored{};
        const size_t copy = (std::min)(sizeof(stored), sizeof(WAVEFORMATEX) + format->cbSize);
        std::memcpy(&stored, format, copy);
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
    if (installed_) {
        installed_ = 0;
        DetachExport(reinterpret_cast<void**>(&origin_XAudio2Create),
                     reinterpret_cast<void*>(&Hook_XAudio2Create));
    }
    // Restore every patched vtable slot so nothing dangles after unload.
    {
        std::lock_guard lock(g_voice_mu);
        for (auto& [vtbl, orig] : g_orig_create_voice) {
            RestoreSlot(&static_cast<Vtable*>(vtbl)->func[kIxAudio2CreateSourceVoice],
                        reinterpret_cast<void*>(&Hook_CreateSourceVoice),
                        reinterpret_cast<void*>(orig));
        }
        for (auto& [vtbl, orig] : g_orig_submit) {
            RestoreSlot(&static_cast<Vtable*>(vtbl)->func[kSourceVoiceSubmitBuffer],
                        reinterpret_cast<void*>(&Hook_SubmitSourceBuffer),
                        reinterpret_cast<void*>(orig));
        }
        g_orig_create_voice.clear();
        g_orig_submit.clear();
        g_voice_fmt.clear();
    }
    LOGI("XAudio2 hooks removed");
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
        // Also patch the source-voice vtable proactively: voices the game
        // created BEFORE injection share this class vtable, and the lazy
        // HookVoiceSubmit path only fires on new CreateSourceVoice calls.
        // Creating a voice is graph-local (no mastering voice, nothing is
        // started), so this cannot silence the game's audio.
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = 48000;
        fmt.nAvgBytesPerSec = 48000 * 8;
        fmt.nBlockAlign = 8;
        fmt.wBitsPerSample = 32;
        IXAudio2SourceVoice* voice = nullptr;
        // tmp's vtable is patched by now, so this runs through
        // Hook_CreateSourceVoice → HookVoiceSubmit.
        if (SUCCEEDED(tmp->CreateSourceVoice(&voice, &fmt)) && voice) {
            HookVoiceSubmit(voice);  // no-op if the hook already patched it
            voice->DestroyVoice();
            std::lock_guard lock(g_voice_mu);
            g_voice_fmt.erase(voice);  // probe leftover, pointer is dead now
            LOGI("XAudio2: source-voice vtable patched via temp voice");
        } else {
            LOGE("XAudio2: temp CreateSourceVoice failed, voices stay lazy-hooked");
        }
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
