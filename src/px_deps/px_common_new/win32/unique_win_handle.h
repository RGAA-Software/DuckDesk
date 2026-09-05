#ifndef PX_COMMON_NEW_WIN32_UNIQUE_WIN_HANDLE_H
#define PX_COMMON_NEW_WIN32_UNIQUE_WIN_HANDLE_H

#include <Windows.h>

#include <memory>
#include <type_traits>

namespace px {

struct WinHandleCloser final {
    void operator()(void* handle) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): opaque Win32 HANDLE boundary.
        if (handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
};

using UniqueWinHandle = std::unique_ptr<void, WinHandleCloser>;

struct WinIconCloser final {
    void operator()(std::remove_pointer_t<HICON>* icon) const noexcept {  // NOLINT(gammaray-raw-pointer-boundary): HICON ABI.
        if (icon != nullptr) {
            DestroyIcon(icon);
        }
    }
};

using UniqueWinIcon = std::unique_ptr<std::remove_pointer_t<HICON>, WinIconCloser>;

}  // namespace px

#endif  // PX_COMMON_NEW_WIN32_UNIQUE_WIN_HANDLE_H
