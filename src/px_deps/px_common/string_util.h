//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_STRINGEXT_H
#define TC_APPLICATION_STRINGEXT_H

#include <vector>
#include <string>
#include <string_view>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <iomanip>
#include <cstring>
#include <filesystem>

#ifdef WIN32
#include <Windows.h>
#endif

#include "num_formatter.h"

namespace px {

class StringUtil {
  public:
    //
    static void Split(const std::string& s, std::vector<std::string>& sv, const char delim = ' ') {
        sv.clear();
        std::istringstream iss(s);
        std::string temp;
        while (std::getline(iss, temp, delim)) {
            sv.emplace_back(std::move(temp));
        }
    }

    static void Split(const std::string& s, std::vector<std::string>& res, const std::string& delimiter) {
        res.clear();
        if (delimiter.empty()) {
            res.push_back(s);
            return;
        }
        size_t pos_start = 0, pos_end, delim_len = delimiter.length();
        std::string token;
        while ((pos_end = s.find(delimiter, pos_start)) != std::string::npos) {
            token = s.substr(pos_start, pos_end - pos_start);
            pos_start = pos_end + delim_len;
            res.push_back(token);
        }
        res.push_back(s.substr(pos_start));
    }

    static void ToLower(std::string& data) {
        std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    }

    static std::string ToLowerCpy(const std::string& data) {
        std::string target = data;
        std::transform(target.begin(), target.end(), target.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return target;
    }

    static std::string ToUpperCpy(const std::string& data) {
        std::string target = data;
        std::transform(target.begin(), target.end(), target.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        return target;
    }

    static bool StartWith(const std::string& input, const std::string& find) {
        if (input.rfind(find, 0) == 0) {
            return true;
        }
        return false;
    }

    static std::string CopyStr(const std::string& origin) {
        std::string copy;
        copy.resize(origin.size());
        memcpy(copy.data(), origin.data(), origin.size());
        return copy;
    }

    template <size_t N> static bool CopyCStringToArray(char (&dst)[N], std::string_view src) {
        static_assert(N > 0, "destination buffer must not be empty");
        const auto copy_size = std::min(src.size(), N - 1);
        if (copy_size > 0) {
            memcpy(dst, src.data(), copy_size);
        }
        dst[copy_size] = '\0';
        return src.size() >= N;
    }

    static void Replace(std::string& origin, const std::string& from, const std::string& to) {
        if (from.empty()) {
            return;
        }
        size_t start_pos = 0;
        while ((start_pos = origin.find(from, start_pos)) != std::string::npos) {
            origin.replace(start_pos, from.length(), to);
            start_pos += to.length();
        }
    }

    static inline std::wstring ToWString(const std::string& src) {
#ifdef WIN32
        if (src.empty()) {
            return {};
        }
        const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src.data(), static_cast<int>(src.size()), nullptr, 0);
        if (required <= 0) {
            return {};
        }
        std::wstring result(static_cast<size_t>(required), L'\0');
        const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src.data(), static_cast<int>(src.size()), result.data(), required);
        if (written <= 0) {
            return {};
        }
        return result;
#else
        std::wstring result;
        result.reserve(src.size());
        size_t offset = 0;
        while (offset < src.size()) {
            const auto lead = static_cast<uint8_t>(src[offset]);
            uint32_t code_point = 0;
            size_t continuation_count = 0;
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
                return {};
            }

            if (offset + continuation_count >= src.size()) {
                return {};
            }
            for (size_t index = 1; index <= continuation_count; ++index) {
                const auto continuation = static_cast<uint8_t>(src[offset + index]);
                if ((continuation & 0xc0) != 0x80) {
                    return {};
                }
                code_point = (code_point << 6) | (continuation & 0x3f);
            }
            const bool overlong = (continuation_count == 1 && code_point < 0x80) || (continuation_count == 2 && code_point < 0x800) ||
                                  (continuation_count == 3 && code_point < 0x10000);
            if (overlong || code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
                return {};
            }

#if WCHAR_MAX <= 0xffff
            if (code_point <= 0xffff) {
                result.push_back(static_cast<wchar_t>(code_point));
            } else {
                code_point -= 0x10000;
                result.push_back(static_cast<wchar_t>(0xd800 + (code_point >> 10)));
                result.push_back(static_cast<wchar_t>(0xdc00 + (code_point & 0x3ff)));
            }
#else
            result.push_back(static_cast<wchar_t>(code_point));
#endif
            offset += continuation_count + 1;
        }
        return result;
#endif
    }

    static inline std::string ToUTF8(const std::wstring& src) {
#ifdef WIN32
        if (src.empty()) {
            return {};
        }
        const int required =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src.data(), static_cast<int>(src.size()), nullptr, 0, nullptr, nullptr);
        if (required <= 0) {
            return {};
        }
        std::string result(static_cast<size_t>(required), '\0');
        const int written =
            WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src.data(), static_cast<int>(src.size()), result.data(), required, nullptr, nullptr);
        if (written <= 0) {
            return {};
        }
        return result;
#else
        std::string result;
        result.reserve(src.size());
        for (size_t index = 0; index < src.size(); ++index) {
            uint32_t code_point = static_cast<uint32_t>(src[index]);
#if WCHAR_MAX <= 0xffff
            if (code_point >= 0xd800 && code_point <= 0xdbff) {
                if (++index >= src.size()) {
                    return {};
                }
                const auto low = static_cast<uint32_t>(src[index]);
                if (low < 0xdc00 || low > 0xdfff) {
                    return {};
                }
                code_point = 0x10000 + ((code_point - 0xd800) << 10) + (low - 0xdc00);
            } else if (code_point >= 0xdc00 && code_point <= 0xdfff) {
                return {};
            }
#endif
            if (code_point > 0x10ffff || (code_point >= 0xd800 && code_point <= 0xdfff)) {
                return {};
            }
            AppendUtf8(result, code_point);
        }
        return result;
#endif
    }

#ifdef WIN32
    static std::string GetErrorStr(HRESULT hr) {
        wchar_t buffer[4096] = {0};
        FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL, hr, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buffer,
                       sizeof(buffer) / sizeof(*buffer), NULL);
        std::wstring res = buffer;
        return ToUTF8(res);
    }

    static std::string StandardizeWinPath(const std::string& path) {
        std::string normalized = path;
        StringUtil::Replace(normalized, "\\", "/");
        // D: => D:/
        if (normalized.size() == 2 && normalized[1] == ':') {
            normalized += "/";
        }
        // "D:/video/" => "D:/video"
        if (normalized.size() >= 4 && normalized.back() == '/') {
            normalized.pop_back();
        }
        return normalized;
    }
#endif
    static std::string FormatSize(uint64_t byte_size) {
        //            static const char* suffixes[] = { "B", "KB", "MB", "GB" };
        //            const int numSuffixes = sizeof(suffixes) / sizeof(suffixes[0]);
        //
        //            double size = static_cast<double>(byte_size);
        //            int suffixIndex = 0;
        //
        //            while (size >= 1024.0 && suffixIndex < numSuffixes - 1) {
        //                size /= 1024.0;
        //                ++suffixIndex;
        //            }
        //
        //            std::ostringstream stream;
        //            stream << std::fixed << std::setprecision(2) << byte_size << " " << suffixes[suffixIndex];
        //            return stream.str();
        return NumFormatter::FormatStorageSize(byte_size);
    }

    static std::string Trim(const std::string& str);

    static bool IsValidInteger(const std::string& str) {
        if (str.empty())
            return false;
        for (const unsigned char character : str) {
            if (!std::isdigit(character)) {
                return false;
            }
        }
        return true;
    }

    static std::string ToHexString(const std::vector<uint8_t>& data);

  private:
    static void AppendUtf8(std::string& result, uint32_t code_point) {
        if (code_point <= 0x7f) {
            result.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7ff) {
            result.push_back(static_cast<char>(0xc0 | (code_point >> 6)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        } else if (code_point <= 0xffff) {
            result.push_back(static_cast<char>(0xe0 | (code_point >> 12)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        } else {
            result.push_back(static_cast<char>(0xf0 | (code_point >> 18)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3f)));
            result.push_back(static_cast<char>(0x80 | (code_point & 0x3f)));
        }
    }
};

// Helper: create std::filesystem::path from UTF-8 encoded std::string
inline std::filesystem::path PathFromUTF8(const std::string& s) {
#ifdef WIN32
    return std::filesystem::path(StringUtil::ToWString(s));
#else
    return std::filesystem::path(s);
#endif
}

// Helper: convert std::filesystem::path to UTF-8 encoded std::string
inline std::string PathToUTF8(const std::filesystem::path& p) {
#ifdef WIN32
    return StringUtil::ToUTF8(p.wstring());
#else
    return p.string();
#endif
}

#if defined(__cpp_char8_t)
template <std::size_t Size> [[nodiscard]] inline std::string Utf8String(const char8_t (&value)[Size]) {
    std::string result;
    result.reserve(Size - 1);
    for (std::size_t index = 0; index + 1 < Size; ++index) {
        result.push_back(static_cast<char>(value[index]));
    }
    return result;
}
#else
template <std::size_t Size> [[nodiscard]] inline std::string Utf8String(const char (&value)[Size]) {
    return std::string(value, Size - 1);
}
#endif

} // namespace px

#endif // TC_APPLICATION_STRINGEXT_H
