#pragma once

#include <optional>
#include <string>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

namespace px {

// The process handle is borrowed for the duration of this synchronous Win32 boundary call.
[[nodiscard]] std::optional<std::wstring> ReadProcessCommandLine(HANDLE process); // NOLINT(gammaray-raw-pointer-boundary): borrowed HANDLE.

} // namespace px
