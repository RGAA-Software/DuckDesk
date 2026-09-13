#include "account_avatar_picker.h"

#include <Windows.h>
#include <commdlg.h>

#include <array>
#include <string_view>
#include <utility>

namespace px::panel::ui {
namespace {

std::string WideToUtf8(const std::wstring_view value) {
    if (value.empty())
        return {};
    const int count{WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr)};
    if (count <= 0)
        return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count, nullptr, nullptr) !=
        count) {
        return {};
    }
    return result;
}

} // namespace

std::optional<std::string> PickAvatarImage() {
    std::array<wchar_t, 32'768> path{};
    OPENFILENAMEW request{}; // NOLINT(gammaray-raw-pointer-boundary): Win32 common-dialog ABI structure.
    request.lStructSize = sizeof(request);
    request.lpstrFilter = L"Images (*.png;*.jpg;*.jpeg;*.webp)\0*.png;*.jpg;*.jpeg;*.webp\0All files (*.*)\0*.*\0";
    request.lpstrFile = path.data();
    request.nMaxFile = static_cast<DWORD>(path.size());
    request.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
    if (GetOpenFileNameW(&request) == FALSE)
        return std::nullopt;
    auto result = WideToUtf8(path.data());
    return result.empty() ? std::nullopt : std::optional{std::move(result)};
}

} // namespace px::panel::ui
