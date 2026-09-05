#include "HookDirectSound.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef DIRECTSOUND_VERSION
#define DIRECTSOUND_VERSION 0x0800
#endif
#include <Windows.h>
#include <mmsystem.h>
#include <mmreg.h>
#include <detours/detours.h>
#include <dsound.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "AudioHookCommon.h"
#include "px_common/log.h"

#pragma comment(lib, "dsound.lib")

namespace px {
namespace {

struct Vtable {
    void* func[32];
};
struct ComObj {
    Vtable* vtbl;
};

// IDirectSound8: after IUnknown — CreateSoundBuffer is index 3
constexpr int kDSCreateSoundBuffer = 3;
// IDirectSoundBuffer: Lock=11, Unlock=19 (see dsound.h method order)
constexpr int kDSBLock = 11;
constexpr int kDSBUnlock = 19;

using DirectSoundCreate8Fn = HRESULT(WINAPI*)(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN);
using CreateSoundBufferFn = HRESULT(STDMETHODCALLTYPE*)(IDirectSound8*,
                                                        LPCDSBUFFERDESC,
                                                        LPDIRECTSOUNDBUFFER*,
                                                        LPUNKNOWN);
using LockFn = HRESULT(STDMETHODCALLTYPE*)(IDirectSoundBuffer*,
                                           DWORD,
                                           DWORD,
                                           LPVOID*,
                                           LPDWORD,
                                           LPVOID*,
                                           LPDWORD,
                                           DWORD);
using UnlockFn = HRESULT(STDMETHODCALLTYPE*)(IDirectSoundBuffer*, LPVOID, DWORD, LPVOID, DWORD);

DirectSoundCreate8Fn origin_DirectSoundCreate8 = nullptr;
CreateSoundBufferFn origin_CreateSoundBuffer = nullptr;
void* g_create_vtbl = nullptr;  // IDirectSound8 vtbl whose slot 3 we patched

struct FmtInfo {
    int rate = 0;
    int channels = 0;
    SimpleAudioFormat af = SimpleAudioFormat::kPCM_S16;
    // False when the buffer format is unknown/unsupported — drop, never guess.
    bool ok = false;
};

struct LockState {
    void* ptr1 = nullptr;
    DWORD bytes1 = 0;
    void* ptr2 = nullptr;
    DWORD bytes2 = 0;
    FmtInfo fmt{};
};

std::mutex g_mu;
std::unordered_map<IDirectSoundBuffer*, FmtInfo> g_buf_fmt;
std::unordered_map<IDirectSoundBuffer*, LockState> g_locks;
// Per-vtable originals: different IDirectSoundBuffer classes have their own
// vtables, each keeps its own true Lock/Unlock.
std::unordered_map<void*, LockFn> g_orig_lock;
std::unordered_map<void*, UnlockFn> g_orig_unlock;
std::atomic<bool> g_create_hooked{false};
std::atomic<uint64_t> g_unlocks{0};
std::atomic<uint64_t> g_bad_fmt{0};

bool AttachOne(void** target, void* detour) {
    DetourTransactionBegin();
    DetourUpdateAllThreads();
    if (DetourAttach(target, detour) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
}

bool DetachOne(void** target, void* detour) {
    DetourTransactionBegin();
    DetourUpdateAllThreads();
    if (DetourDetach(target, detour) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
}

// Caller must hold g_mu.
bool PatchSlot(void** slot, void* detour, void** saved) {
    if (!slot || !detour || !saved || !*slot) {
        return false;
    }
    if (*slot == detour) {
        return true;
    }
    DWORD old = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_EXECUTE_READWRITE, &old)) {
        return false;
    }
    *saved = *slot;
    *slot = detour;
    VirtualProtect(slot, sizeof(void*), old, &old);
    return true;
}

// Caller must hold g_mu.
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

HRESULT STDMETHODCALLTYPE Hook_Unlock(IDirectSoundBuffer* thiz,
                                      LPVOID ptr1,
                                      DWORD bytes1,
                                      LPVOID ptr2,
                                      DWORD bytes2) {
    FmtInfo fmt{};
    UnlockFn orig = nullptr;
    {
        std::lock_guard lock(g_mu);
        auto it = g_buf_fmt.find(thiz);
        if (it != g_buf_fmt.end()) {
            fmt = it->second;
        }
        g_locks.erase(thiz);
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        if (obj && obj->vtbl) {
            auto oit = g_orig_unlock.find(obj->vtbl);
            if (oit != g_orig_unlock.end()) {
                orig = oit->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto push_part = [&](void* p, DWORD n) {
        if (!p || n == 0 || !fmt.ok) {
            return;
        }
        PushHookedPcm(p, static_cast<int>(n), fmt.af, fmt.rate, fmt.channels, "DirectSound");
    };
    push_part(ptr1, bytes1);
    push_part(ptr2, bytes2);
    if (!fmt.ok && (bytes1 > 0 || bytes2 > 0)) {
        const auto n = g_bad_fmt.fetch_add(1, std::memory_order_relaxed) + 1;
        if (n == 1 || (n % 200) == 0) {
            LOGW("DirectSound Unlock: drop frame, unknown format n={}", n);
        }
    }
    const auto n = g_unlocks.fetch_add(1) + 1;
    if (n == 1 || (n % 100) == 0) {
        LOGI("DirectSound Unlock: calls={} {}Hz {}ch", n, fmt.rate, fmt.channels);
    }
    return orig(thiz, ptr1, bytes1, ptr2, bytes2);
}

HRESULT STDMETHODCALLTYPE Hook_Lock(IDirectSoundBuffer* thiz,
                                    DWORD offset,
                                    DWORD bytes,
                                    LPVOID* pp1,
                                    LPDWORD pcb1,
                                    LPVOID* pp2,
                                    LPDWORD pcb2,
                                    DWORD flags) {
    LockFn orig = nullptr;
    {
        std::lock_guard lock(g_mu);
        auto* obj = reinterpret_cast<ComObj*>(thiz);
        if (obj && obj->vtbl) {
            auto it = g_orig_lock.find(obj->vtbl);
            if (it != g_orig_lock.end()) {
                orig = it->second;
            }
        }
    }
    if (!orig) {
        return E_POINTER;
    }
    auto hr = orig(thiz, offset, bytes, pp1, pcb1, pp2, pcb2, flags);
    if (SUCCEEDED(hr)) {
        LockState st;
        if (pp1 && pcb1) {
            st.ptr1 = *pp1;
            st.bytes1 = *pcb1;
        }
        if (pp2 && pcb2) {
            st.ptr2 = *pp2;
            st.bytes2 = *pcb2;
        }
        {
            std::lock_guard lock(g_mu);
            auto it = g_buf_fmt.find(thiz);
            if (it != g_buf_fmt.end()) {
                st.fmt = it->second;
            }
            g_locks[thiz] = st;
        }
    }
    return hr;
}

void HookBuffer(IDirectSoundBuffer* buf, const WAVEFORMATEX* fmt) {
    if (!buf) {
        return;
    }
    auto* obj = reinterpret_cast<ComObj*>(buf);
    if (!obj || !obj->vtbl) {
        return;
    }
    // Whole patch sequence under g_mu: two racing HookBuffer calls must not
    // leave a patched slot without its saved original.
    std::lock_guard lock(g_mu);
    if (fmt) {
        FmtInfo stored;
        stored.rate = static_cast<int>(fmt->nSamplesPerSec);
        stored.channels = static_cast<int>(fmt->nChannels);
        stored.ok = ResolveWaveFormat(fmt, stored.af);
        g_buf_fmt[buf] = stored;
    }
    if (g_orig_lock.count(obj->vtbl)) {
        return;
    }
    void* saved_lock = nullptr;
    void* saved_unlock = nullptr;
    if (PatchSlot(&obj->vtbl->func[kDSBLock], reinterpret_cast<void*>(&Hook_Lock),
                  &saved_lock) &&
        PatchSlot(&obj->vtbl->func[kDSBUnlock], reinterpret_cast<void*>(&Hook_Unlock),
                  &saved_unlock)) {
        if (saved_lock && saved_unlock) {
            g_orig_lock[obj->vtbl] = reinterpret_cast<LockFn>(saved_lock);
            g_orig_unlock[obj->vtbl] = reinterpret_cast<UnlockFn>(saved_unlock);
            LOGI("DirectSound Lock/Unlock hooked vtbl={}", static_cast<const void*>(obj->vtbl));
        }
    }
}

HRESULT STDMETHODCALLTYPE Hook_CreateSoundBuffer(IDirectSound8* thiz,
                                                 LPCDSBUFFERDESC desc,
                                                 LPDIRECTSOUNDBUFFER* pp,
                                                 LPUNKNOWN unk) {
    auto hr = origin_CreateSoundBuffer(thiz, desc, pp, unk);
    if (SUCCEEDED(hr) && pp && *pp) {
        const WAVEFORMATEX* fmt = nullptr;
        if (desc) {
            fmt = desc->lpwfxFormat;
        }
        // Primary buffer may omit format; query later via GetFormat if needed.
        HookBuffer(*pp, fmt);
        if (fmt) {
            LOGI("DirectSound CreateSoundBuffer: {}Hz {}ch {}bit", fmt->nSamplesPerSec,
                 fmt->nChannels, fmt->wBitsPerSample);
        } else {
            // Try GetFormat on the buffer (vtable index 5).
            WAVEFORMATEXTENSIBLE got{};
            DWORD written = 0;
            if (SUCCEEDED((*pp)->GetFormat(&got.Format, sizeof(got), &written)) &&
                got.Format.nSamplesPerSec) {
                HookBuffer(*pp, &got.Format);
                LOGI("DirectSound CreateSoundBuffer via GetFormat: {}Hz {}ch",
                     got.Format.nSamplesPerSec, got.Format.nChannels);
            } else {
                LOGI("DirectSound CreateSoundBuffer: (format unknown yet)");
            }
        }
    }
    return hr;
}

HRESULT WINAPI Hook_DirectSoundCreate8(LPCGUID guid, LPDIRECTSOUND8* pp, LPUNKNOWN unk) {
    auto hr = origin_DirectSoundCreate8(guid, pp, unk);
    if (SUCCEEDED(hr) && pp && *pp) {
        auto* obj = reinterpret_cast<ComObj*>(*pp);
        if (obj && obj->vtbl) {
            std::lock_guard lock(g_mu);
            if (!g_create_hooked.load(std::memory_order_acquire)) {
                void* saved = nullptr;
                if (PatchSlot(&obj->vtbl->func[kDSCreateSoundBuffer],
                              reinterpret_cast<void*>(&Hook_CreateSoundBuffer), &saved) &&
                    saved) {
                    origin_CreateSoundBuffer = reinterpret_cast<CreateSoundBufferFn>(saved);
                    g_create_vtbl = obj->vtbl;
                    g_create_hooked.store(true, std::memory_order_release);
                    LOGI("DirectSound CreateSoundBuffer hooked");
                }
            }
        }
    }
    return hr;
}

}  // namespace

HookDirectSound* HookDirectSound::Instance() {
    static HookDirectSound inst;
    return &inst;
}

bool HookDirectSound::Start(std::shared_ptr<AudioMixer> mixer) {
    mixer_ = std::move(mixer);
    if (TryInstall()) {
        return true;
    }
    HANDLE th = CreateThread(nullptr, 0,
                             [](LPVOID p) -> DWORD {
                                 reinterpret_cast<HookDirectSound*>(p)->WatcherMain();
                                 return 0;
                             },
                             this, 0, nullptr);
    watcher_ = th;
    LOGI("DirectSound: watcher started");
    return true;
}

void HookDirectSound::Stop() {
    stop_ = 1;
    if (watcher_) {
        WaitForSingleObject(static_cast<HANDLE>(watcher_), 2000);
        CloseHandle(static_cast<HANDLE>(watcher_));
        watcher_ = nullptr;
    }
    if (installed_) {
        installed_ = 0;
        DetachOne(reinterpret_cast<void**>(&origin_DirectSoundCreate8),
                  reinterpret_cast<void*>(&Hook_DirectSoundCreate8));
    }
    // Restore every patched vtable slot.
    {
        std::lock_guard lock(g_mu);
        if (g_create_vtbl && origin_CreateSoundBuffer) {
            RestoreSlot(&static_cast<Vtable*>(g_create_vtbl)->func[kDSCreateSoundBuffer],
                        reinterpret_cast<void*>(&Hook_CreateSoundBuffer),
                        reinterpret_cast<void*>(origin_CreateSoundBuffer));
            g_create_vtbl = nullptr;
            origin_CreateSoundBuffer = nullptr;
        }
        for (auto& [vtbl, orig] : g_orig_lock) {
            RestoreSlot(&static_cast<Vtable*>(vtbl)->func[kDSBLock],
                        reinterpret_cast<void*>(&Hook_Lock), reinterpret_cast<void*>(orig));
        }
        for (auto& [vtbl, orig] : g_orig_unlock) {
            RestoreSlot(&static_cast<Vtable*>(vtbl)->func[kDSBUnlock],
                        reinterpret_cast<void*>(&Hook_Unlock), reinterpret_cast<void*>(orig));
        }
        g_orig_lock.clear();
        g_orig_unlock.clear();
        g_buf_fmt.clear();
        g_locks.clear();
        g_create_hooked.store(false, std::memory_order_release);
    }
    LOGI("DirectSound hooks removed");
}

void HookDirectSound::WatcherMain() {
    for (int i = 0; i < 600 && !stop_; i++) {
        if (TryInstall()) {
            return;
        }
        Sleep(100);
    }
}

bool HookDirectSound::TryInstall() {
    if (installed_) {
        return true;
    }
    HMODULE mod = GetModuleHandleW(L"dsound.dll");
    if (!mod) {
        return false;
    }
    auto* create =
        reinterpret_cast<DirectSoundCreate8Fn>(GetProcAddress(mod, "DirectSoundCreate8"));
    if (!create) {
        create = reinterpret_cast<DirectSoundCreate8Fn>(GetProcAddress(mod, "DirectSoundCreate"));
    }
    if (!create) {
        return false;
    }
    origin_DirectSoundCreate8 = create;
    if (!AttachOne(reinterpret_cast<void**>(&origin_DirectSoundCreate8),
                   reinterpret_cast<void*>(&Hook_DirectSoundCreate8))) {
        LOGE("DirectSound: DetourAttach failed");
        return false;
    }
    installed_ = 1;
    LOGI("DirectSoundCreate8 hooked");

    // Proactively patch the IDirectSound / IDirectSoundBuffer class vtables:
    // objects the game created BEFORE injection share them, and the lazy
    // HookBuffer path only fires for newly created buffers. A tiny secondary
    // buffer is created and released immediately — never played.
    IDirectSound8* ds = nullptr;
    if (SUCCEEDED(Hook_DirectSoundCreate8(nullptr, &ds, nullptr)) && ds) {
        WAVEFORMATEX fmt{};
        fmt.wFormatTag = WAVE_FORMAT_PCM;
        fmt.nChannels = 2;
        fmt.nSamplesPerSec = 48000;
        fmt.wBitsPerSample = 16;
        fmt.nBlockAlign = 4;
        fmt.nAvgBytesPerSec = 48000 * 4;
        DSBUFFERDESC desc{};
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DSBCAPS_CTRLVOLUME;
        desc.dwBufferBytes = fmt.nAvgBytesPerSec / 100;  // ~10ms
        desc.lpwfxFormat = &fmt;
        IDirectSoundBuffer* buf = nullptr;
        // ds vtable is patched by now → runs through Hook_CreateSoundBuffer
        // → HookBuffer patches the shared IDirectSoundBuffer vtable.
        if (SUCCEEDED(ds->CreateSoundBuffer(&desc, &buf, nullptr)) && buf) {
            {
                std::lock_guard lock(g_mu);
                g_buf_fmt.erase(buf);  // probe leftover, pointer is dead now
            }
            buf->Release();
            LOGI("DirectSound: buffer vtable patched via temp buffer");
        } else {
            LOGE("DirectSound: temp CreateSoundBuffer failed, buffers stay lazy-hooked");
        }
        ds->Release();
    } else {
        LOGE("DirectSound: temp DirectSoundCreate8 failed, buffers stay lazy-hooked");
    }
    return true;
}

}  // namespace px
