#pragma once

#ifdef WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#endif

namespace tc {

// True when WASAPI process-loopback (OBS Application Audio Capture) is available.
// Matches OBS gate: Windows 10 version 2004 (build 19041) or newer.
inline bool IsProcessLoopbackCaptureSupported() {
#ifdef WIN32
    using RtlGetVersionFn = LONG(WINAPI*)(PRTL_OSVERSIONINFOW);
    auto ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        return false;
    }
    auto rtl_get_version = reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion"));
    if (!rtl_get_version) {
        return false;
    }
    RTL_OSVERSIONINFOW vi{};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (rtl_get_version(&vi) != 0) {
        return false;
    }
    constexpr DWORD kMinBuild = 19041;
    if (vi.dwMajorVersion > 10) {
        return true;
    }
    if (vi.dwMajorVersion == 10 && vi.dwBuildNumber >= kMinBuild) {
        return true;
    }
    return false;
#else
    return false;
#endif
}

// Test/debug: set GODESK_FORCE_HOOK_AUDIO=1 to force in-process hook and skip host PID loopback.
inline bool ForceInProcessHookAudio() {
#ifdef WIN32
    char buf[16] = {};
    const DWORD n = GetEnvironmentVariableA("GODESK_FORCE_HOOK_AUDIO", buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) {
        return false;
    }
    return buf[0] == '1' || buf[0] == 'y' || buf[0] == 'Y' || buf[0] == 't' || buf[0] == 'T';
#else
    return false;
#endif
}

// Production preference: process-loopback when supported, unless forced to hook.
inline bool PreferProcessLoopbackCapture() {
    if (ForceInProcessHookAudio()) {
        return false;
    }
    return IsProcessLoopbackCaptureSupported();
}

}  // namespace tc
