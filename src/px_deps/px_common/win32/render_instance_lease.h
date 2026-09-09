#pragma once
#include "unique_win_handle.h"
#include <cstdint>
#include <format>

namespace px {
inline UniqueWinHandle AcquireRenderInstanceLease(std::uint16_t port, DWORD& error) {
    error = ERROR_INVALID_PARAMETER;
    if (port == 0) { return {}; }
    // Kernel object names are not filesystem paths; a backslash is permitted
    // only in the Local/Global prefix. TEMP\name would fail with error 123.
    const auto name = std::format(L"Local\\GammaRay.Render.{}", port);
    auto lease = UniqueWinHandle{CreateMutexW(nullptr, FALSE, name.c_str())};
    error = GetLastError();
    if (!lease || error == ERROR_ALREADY_EXISTS) { return {}; }
    error = ERROR_SUCCESS;
    return lease;
}
} // namespace px
