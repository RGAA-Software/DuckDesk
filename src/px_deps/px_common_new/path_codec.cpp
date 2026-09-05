#include "path_codec.h"

#include <climits>
#include <cstdint>
#include <limits>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace px {
namespace {

PxAsyncError MakePathEncodingError(std::string message, std::string detail_code) {
    return MakePxAsyncError(PxAsyncErrorCode::kInvalidArgument, "path.encoding", std::move(message), false, std::move(detail_code));
}

} // namespace

bool IsValidUtf8(std::string_view value) noexcept {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto lead = static_cast<std::uint8_t>(value[offset]);
        std::uint32_t code_point = 0;
        std::size_t continuation_count = 0;
        if (lead <= 0x7f) {
            code_point = lead;
        } else if (lead >= 0xc2 && lead <= 0xdf) {
            code_point = lead & 0x1f;
            continuation_count = 1;
        } else if (lead >= 0xe0 && lead <= 0xef) {
            code_point = lead & 0x0f;
            continuation_count = 2;
        } else if (lead >= 0xf0 && lead <= 0xf4) {
            code_point = lead & 0x07;
            continuation_count = 3;
        } else {
            return false;
        }

        if (continuation_count >= value.size() - offset) {
            return false;
        }
        for (std::size_t index = 1; index <= continuation_count; ++index) {
            const auto continuation = static_cast<std::uint8_t>(value[offset + index]);
            if ((continuation & 0xc0) != 0x80) {
                return false;
            }
            code_point = (code_point << 6) | (continuation & 0x3f);
        }

        const bool overlong = (continuation_count == 1 && code_point < 0x80) || (continuation_count == 2 && code_point < 0x800)
            || (continuation_count == 3 && code_point < 0x10000);
        if (overlong || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
            return false;
        }
        offset += continuation_count + 1;
    }
    return true;
}

PxResult<std::filesystem::path> PathFromUtf8(std::string_view utf8) {
    if (!IsValidUtf8(utf8)) {
        return PxResult<std::filesystem::path>::Failure(MakePathEncodingError("path contains invalid UTF-8", "PATH_INVALID_UTF8"));
    }
    if (utf8.empty()) {
        return PxResult<std::filesystem::path>::Success({});
    }

#if defined(_WIN32)
    if (utf8.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return PxResult<std::filesystem::path>::Failure(MakePathEncodingError("UTF-8 path is too long to convert", "PATH_TOO_LONG"));
    }
    const auto input_size = static_cast<int>(utf8.size());
    const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), input_size, nullptr, 0);
    if (required <= 0) {
        return PxResult<std::filesystem::path>::Failure(
            MakePathEncodingError("Windows rejected the UTF-8 path", "PATH_UTF8_TO_NATIVE_FAILED"));
    }
    std::wstring native(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8.data(), input_size, native.data(), required) != required) {
        return PxResult<std::filesystem::path>::Failure(
            MakePathEncodingError("Windows failed to convert the complete UTF-8 path", "PATH_UTF8_TO_NATIVE_FAILED"));
    }
    return PxResult<std::filesystem::path>::Success(std::filesystem::path(std::move(native)));
#else
    return PxResult<std::filesystem::path>::Success(std::filesystem::path(utf8.begin(), utf8.end()));
#endif
}

PxResult<std::string> PathToUtf8(const std::filesystem::path& path) {
#if defined(_WIN32)
    const auto& native = path.native();
    if (native.empty()) {
        return PxResult<std::string>::Success({});
    }
    if (native.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return PxResult<std::string>::Failure(MakePathEncodingError("native path is too long to convert", "PATH_TOO_LONG"));
    }
    const auto input_size = static_cast<int>(native.size());
    const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), input_size, nullptr, 0, nullptr, nullptr);
    if (required <= 0) {
        return PxResult<std::string>::Failure(
            MakePathEncodingError("Windows native path contains invalid UTF-16", "PATH_NATIVE_TO_UTF8_FAILED"));
    }
    std::string utf8(static_cast<std::size_t>(required), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, native.data(), input_size, utf8.data(), required, nullptr, nullptr) != required) {
        return PxResult<std::string>::Failure(
            MakePathEncodingError("Windows failed to convert the complete native path", "PATH_NATIVE_TO_UTF8_FAILED"));
    }
    return PxResult<std::string>::Success(std::move(utf8));
#else
    const auto& native = path.native();
    if (!IsValidUtf8(native)) {
        return PxResult<std::string>::Failure(MakePathEncodingError("native path is not valid UTF-8", "PATH_NATIVE_INVALID_UTF8"));
    }
    return PxResult<std::string>::Success(native);
#endif
}

} // namespace px
