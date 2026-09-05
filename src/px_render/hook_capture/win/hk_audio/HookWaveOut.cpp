#include "HookWaveOut.h"

#include <Windows.h>
#include <detours/detours.h>
#include <mmsystem.h>
#include <mmreg.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "AudioHookCommon.h"
#include "px_common/log.h"

#pragma comment(lib, "winmm.lib")

namespace px {
namespace {

using WaveOutOpenFn = MMRESULT(WINAPI*)(LPHWAVEOUT, UINT, LPCWAVEFORMATEX, DWORD_PTR, DWORD_PTR,
                                        DWORD);
using WaveOutWriteFn = MMRESULT(WINAPI*)(HWAVEOUT, LPWAVEHDR, UINT);
using PlaySoundWFn = BOOL(WINAPI*)(LPCWSTR, HMODULE, DWORD);

WaveOutOpenFn origin_waveOutOpen = nullptr;
WaveOutWriteFn origin_waveOutWrite = nullptr;
PlaySoundWFn origin_PlaySoundW = nullptr;
bool g_playsound_hooked = false;

std::mutex g_mu;
// Full extensible copy so SubFormat survives (cbSize=0 truncation misjudged
// 32-bit int PCM as float).
std::unordered_map<HWAVEOUT, WAVEFORMATEXTENSIBLE> g_fmt;
std::atomic<uint64_t> g_writes{0};
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

MMRESULT WINAPI Hook_waveOutOpen(LPHWAVEOUT phwo, UINT uDeviceID, LPCWAVEFORMATEX pwfx,
                                 DWORD_PTR dwCallback, DWORD_PTR dwInstance, DWORD fdwOpen) {
    auto mr = origin_waveOutOpen(phwo, uDeviceID, pwfx, dwCallback, dwInstance, fdwOpen);
    if (mr == MMSYSERR_NOERROR && phwo && *phwo && pwfx) {
        WAVEFORMATEXTENSIBLE fmt{};
        const size_t copy = (std::min)(sizeof(fmt), sizeof(WAVEFORMATEX) + pwfx->cbSize);
        std::memcpy(&fmt, pwfx, copy);
        std::lock_guard lock(g_mu);
        g_fmt[*phwo] = fmt;
        LOGI("waveOutOpen: device={} {}Hz {}ch {}bit handle={}", uDeviceID, fmt.Format.nSamplesPerSec,
             fmt.Format.nChannels, fmt.Format.wBitsPerSample, static_cast<const void*>(*phwo));
    }
    return mr;
}

MMRESULT WINAPI Hook_waveOutWrite(HWAVEOUT hwo, LPWAVEHDR pwh, UINT cbwh) {
    if (hwo && pwh && pwh->lpData && pwh->dwBufferLength > 0) {
        WAVEFORMATEXTENSIBLE fmt{};
        bool have_fmt = false;
        {
            std::lock_guard lock(g_mu);
            auto it = g_fmt.find(hwo);
            if (it != g_fmt.end()) {
                fmt = it->second;
                have_fmt = true;
            }
        }
        SimpleAudioFormat af = SimpleAudioFormat::kPCM_S16;
        const bool ok = have_fmt && ResolveWaveFormat(&fmt.Format, af);
        if (ok) {
            PushHookedPcm(pwh->lpData, static_cast<int>(pwh->dwBufferLength), af,
                          static_cast<int>(fmt.Format.nSamplesPerSec),
                          static_cast<int>(fmt.Format.nChannels), "waveOut");
        } else {
            const auto n = g_bad_fmt.fetch_add(1, std::memory_order_relaxed) + 1;
            if (n == 1 || (n % 200) == 0) {
                LOGW("waveOutWrite: drop frame, unknown format n={}", n);
            }
        }
        const auto n = g_writes.fetch_add(1) + 1;
        if (n == 1 || (n % 100) == 0) {
            LOGI("waveOutWrite: calls={} bytes={}", n, pwh->dwBufferLength);
        }
    }
    return origin_waveOutWrite(hwo, pwh, cbwh);
}

BOOL WINAPI Hook_PlaySoundW(LPCWSTR pszSound, HMODULE hmod, DWORD fdwSound) {
    // Fire-and-forget API; we only log — actual PCM is usually via waveOut underneath.
    if (pszSound) {
        LOGI("PlaySoundW flags=0x{:x}", fdwSound);
    }
    return origin_PlaySoundW(pszSound, hmod, fdwSound);
}

}  // namespace

HookWaveOut* HookWaveOut::Instance() {
    static HookWaveOut inst;
    return &inst;
}

bool HookWaveOut::Start(std::shared_ptr<AudioMixer> mixer) {
    (void)mixer;
    if (installed_) {
        return true;
    }
    HMODULE winmm = GetModuleHandleW(L"winmm.dll");
    if (!winmm) {
        winmm = LoadLibraryW(L"winmm.dll");
    }
    if (!winmm) {
        LOGE("waveOut: winmm.dll missing");
        return false;
    }
    origin_waveOutOpen = reinterpret_cast<WaveOutOpenFn>(GetProcAddress(winmm, "waveOutOpen"));
    origin_waveOutWrite = reinterpret_cast<WaveOutWriteFn>(GetProcAddress(winmm, "waveOutWrite"));
    origin_PlaySoundW = reinterpret_cast<PlaySoundWFn>(GetProcAddress(winmm, "PlaySoundW"));
    if (!origin_waveOutOpen || !origin_waveOutWrite) {
        LOGE("waveOut: exports missing");
        return false;
    }
    if (!AttachOne(reinterpret_cast<void**>(&origin_waveOutOpen),
                   reinterpret_cast<void*>(&Hook_waveOutOpen)) ||
        !AttachOne(reinterpret_cast<void**>(&origin_waveOutWrite),
                   reinterpret_cast<void*>(&Hook_waveOutWrite))) {
        LOGE("waveOut: DetourAttach failed");
        return false;
    }
    if (origin_PlaySoundW) {
        g_playsound_hooked = AttachOne(reinterpret_cast<void**>(&origin_PlaySoundW),
                                       reinterpret_cast<void*>(&Hook_PlaySoundW));
    }
    installed_ = true;
    LOGI("waveOut hooks installed (waveOutOpen/Write + PlaySoundW)");
    return true;
}

void HookWaveOut::Stop() {
    if (!installed_) {
        return;
    }
    installed_ = false;
    DetachOne(reinterpret_cast<void**>(&origin_waveOutOpen),
              reinterpret_cast<void*>(&Hook_waveOutOpen));
    DetachOne(reinterpret_cast<void**>(&origin_waveOutWrite),
              reinterpret_cast<void*>(&Hook_waveOutWrite));
    if (g_playsound_hooked) {
        DetachOne(reinterpret_cast<void**>(&origin_PlaySoundW),
                  reinterpret_cast<void*>(&Hook_PlaySoundW));
        g_playsound_hooked = false;
    }
    {
        std::lock_guard lock(g_mu);
        g_fmt.clear();
    }
    LOGI("waveOut hooks removed");
}

}  // namespace px
