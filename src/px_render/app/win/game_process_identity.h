#pragma once

#include <Windows.h>
#include <filesystem>
#include <string>
#include <optional>
#include <algorithm>
#include <string_view>

namespace px {

// Full executable paths, not basenames, identify a normal game launch. Fail
// closed for missing/relative paths; no filesystem probing or process mutation.
inline std::optional<std::filesystem::path> NormalizeGameExecutable(const std::filesystem::path& path) {
    auto value = path.native();
    if (value.size() >= 2 && value.front() == L'"' && value.back() == L'"') {
        value = value.substr(1, value.size() - 2);
    }
    if (value.empty() || value.find(L'\0') != std::wstring::npos || value.find(L'"') != std::wstring::npos) {
        return {};
    }
    std::replace(value.begin(), value.end(), L'/', L'\\');
    if (value.size() >= 8 && CompareStringOrdinal(value.data(), 8, L"\\\\?\\UNC\\", 8, TRUE) == CSTR_EQUAL) {
        value = L"\\\\" + value.substr(8);
    } else if (value.starts_with(L"\\\\?\\") && value.size() > 6 && value[5] == L':') {
        value = value.substr(4);
    } else if (value.starts_with(L"\\\\?\\") || value.starts_with(L"\\\\.\\")) {
        return {};
    }
    const std::filesystem::path normalized{value};
    return normalized.is_absolute() ? std::optional{normalized.lexically_normal()} : std::nullopt;
}

inline bool SameGameExecutable(const std::filesystem::path& configured, const std::filesystem::path& observed) {
    const auto expected = NormalizeGameExecutable(configured);
    const auto actual = NormalizeGameExecutable(observed);
    if (!expected || !actual) {
        return false;
    }
    return CompareStringOrdinal(expected->c_str(), -1, actual->c_str(), -1, TRUE) == CSTR_EQUAL;
}

// Application name is also passed separately to CreateProcess*. Keep the
// caller's Windows argument quoting intact rather than splitting on whitespace.
inline std::optional<std::wstring> GameCommandLine(const std::filesystem::path& executable, std::wstring_view arguments) {
    const auto path = NormalizeGameExecutable(executable);
    if (!path || arguments.find(L'\0') != std::wstring_view::npos) {
        return {};
    }
    auto command = L"\"" + path->native() + L"\"";
    if (!arguments.empty()) {
        command += L" ";
        command += arguments;
    }
    return command;
}

} // namespace px
