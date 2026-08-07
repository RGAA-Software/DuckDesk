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

#include <atomic>
#include <mutex>
#include <unordered_map>

#include "AudioHookCommon.h"
#include "tc_common_new/log.h"

#pragma comment(lib, "dsound.lib")

namespace tc {
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
LockFn origin_Lock = nullptr;
UnlockFn origin_Unlock = nullptr;

struct FmtInfo {
    int rate = 0;
    int channels = 0;
    int bits = 16;
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
std::atomic<bool> g_create_hooked{false};
std::atomic<bool> g_lock_hooked{false};
std::atomic<uint64_t> g_unlocks{0};

bool AttachOne(void** target, void* detour) {
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if (DetourAttach(target, detour) != NO_ERROR) {
        DetourTransactionAbort();
        return false;
    }
    return DetourTransactionCommit() == NO_ERROR;
}

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

HRESULT STDMETHODCALLTYPE Hook_Unlock(IDirectSoundBuffer* thiz,
                                      LPVOID ptr1,
                                      DWORD bytes1,
                                      LPVOID ptr2,
                                      DWORD bytes2) {
    FmtInfo fmt{};
    {
        std::lock_guard lock(g_mu);
        auto it = g_buf_fmt.find(thiz);
        if (it != g_buf_fmt.end()) {
            fmt = it->second;
        }
        g_locks.erase(thiz);
    }
    auto push_part = [&](void* p, DWORD n) {
        if (!p || n == 0 || fmt.rate == 0) {
            return;
        }
        SimpleAudioFormat af =
            (fmt.bits == 32) ? SimpleAudioFormat::kPCM_F32 : SimpleAudioFormat::kPCM_S16;
        if (n > 0 && !WasapiCaptureActive()) {
            PushHookedPcm(p, static_cast<int>(n), af, fmt.rate, fmt.channels, "DirectSound");
        }
    };
    push_part(ptr1, bytes1);
    push_part(ptr2, bytes2);
    const auto n = g_unlocks.fetch_add(1) + 1;
    if (n == 1 || (n % 100) == 0) {
        LOGI("DirectSound Unlock: calls={} {}Hz {}ch", n, fmt.rate, fmt.channels);
    }
    return origin_Unlock(thiz, ptr1, bytes1, ptr2, bytes2);
}

HRESULT STDMETHODCALLTYPE Hook_Lock(IDirectSoundBuffer* thiz,
                                    DWORD offset,
                                    DWORD bytes,
                                    LPVOID* pp1,
                                    LPDWORD pcb1,
                                    LPVOID* pp2,
                                    LPDWORD pcb2,
                                    DWORD flags) {
    auto hr = origin_Lock(thiz, offset, bytes, pp1, pcb1, pp2, pcb2, flags);
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
    if (fmt) {
        FmtInfo stored;
        stored.rate = static_cast<int>(fmt->nSamplesPerSec);
        stored.channels = static_cast<int>(fmt->nChannels);
        stored.bits = static_cast<int>(fmt->wBitsPerSample);
        std::lock_guard lock(g_mu);
        g_buf_fmt[buf] = stored;
    }
    auto* obj = reinterpret_cast<ComObj*>(buf);
    if (!obj || !obj->vtbl) {
        return;
    }
    if (!g_lock_hooked.load(std::memory_order_acquire)) {
        void* saved_lock = nullptr;
        void* saved_unlock = nullptr;
        if (PatchSlot(&obj->vtbl->func[kDSBLock], reinterpret_cast<void*>(&Hook_Lock),
                      &saved_lock) &&
            PatchSlot(&obj->vtbl->func[kDSBUnlock], reinterpret_cast<void*>(&Hook_Unlock),
                      &saved_unlock)) {
            origin_Lock = reinterpret_cast<LockFn>(saved_lock);
            origin_Unlock = reinterpret_cast<UnlockFn>(saved_unlock);
            g_lock_hooked.store(true, std::memory_order_release);
            LOGI("DirectSound Lock/Unlock hooked");
        }
    } else {
        void* ignore = nullptr;
        PatchSlot(&obj->vtbl->func[kDSBLock], reinterpret_cast<void*>(&Hook_Lock), &ignore);
        PatchSlot(&obj->vtbl->func[kDSBUnlock], reinterpret_cast<void*>(&Hook_Unlock), &ignore);
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
            WAVEFORMATEX got{};
            DWORD written = 0;
            if (SUCCEEDED((*pp)->GetFormat(&got, sizeof(got), &written)) && got.nSamplesPerSec) {
                HookBuffer(*pp, &got);
                LOGI("DirectSound CreateSoundBuffer via GetFormat: {}Hz {}ch", got.nSamplesPerSec,
                     got.nChannels);
            } else {
                LOGI("DirectSound CreateSoundBuffer: (format unknown yet)");
            }
        }
    }
    return hr;
}

HRESULT WINAPI Hook_DirectSoundCreate8(LPCGUID guid, LPDIRECTSOUND8* pp, LPUNKNOWN unk) {
    auto hr = origin_DirectSoundCreate8(guid, pp, unk);
    if (SUCCEEDED(hr) && pp && *pp && !g_create_hooked.load(std::memory_order_acquire)) {
        auto* obj = reinterpret_cast<ComObj*>(*pp);
        if (obj && obj->vtbl) {
            void* saved = nullptr;
            if (PatchSlot(&obj->vtbl->func[kDSCreateSoundBuffer],
                          reinterpret_cast<void*>(&Hook_CreateSoundBuffer), &saved)) {
                origin_CreateSoundBuffer = reinterpret_cast<CreateSoundBufferFn>(saved);
                g_create_hooked.store(true, std::memory_order_release);
                LOGI("DirectSound CreateSoundBuffer hooked");
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
    return true;
}

}  // namespace tc
