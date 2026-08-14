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
#include <iomanip>
#include <cstring>
#include <filesystem>

#ifdef WIN32
#include <Windows.h>
#else
#include <cerrno>
#include <iconv.h>
#endif

#include "num_formatter.h"

namespace tc
{

    class StringUtil {
    public:
        // 
        static void Split(const std::string &s, std::vector<std::string> &sv, const char delim = ' ') {
            sv.clear();
            std::istringstream iss(s);
            std::string temp;
            while (std::getline(iss, temp, delim)) {
                sv.emplace_back(std::move(temp));
            }
        }

        static void Split(const std::string& s, std::vector<std::string>& res, const std::string& delimiter) {
            size_t pos_start = 0, pos_end, delim_len = delimiter.length();
            std::string token;
            while ((pos_end = s.find (delimiter, pos_start)) != std::string::npos) {
                token = s.substr (pos_start, pos_end - pos_start);
                pos_start = pos_end + delim_len;
                res.push_back (token);
            }
            res.push_back (s.substr (pos_start));
        }

        static void ToLower(std::string& data) {
            std::transform(data.begin(), data.end(), data.begin(), [](unsigned char c){ return std::tolower(c); });
        }

        static std::string ToLowerCpy(const std::string& data) {
            std::string target = data;
            std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) -> char { return std::tolower(c); });
            return target;
        }

        static std::string ToUpperCpy(const std::string& data) {
            std::string target = data;
            std::transform(target.begin(), target.end(), target.begin(), [](unsigned char c) -> char { return std::toupper(c); });
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

        template <size_t N>
        static bool CopyCStringToArray(char (&dst)[N], std::string_view src) {
            static_assert(N > 0, "destination buffer must not be empty");
            const auto copy_size = std::min(src.size(), N - 1);
            if (copy_size > 0) {
                memcpy(dst, src.data(), copy_size);
            }
            dst[copy_size] = '\0';
            return src.size() >= N;
        }

        static void Replace(std::string& origin, const std::string& from, const std::string& to) {
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
            const int required = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src.data(),
                                                     static_cast<int>(src.size()), nullptr, 0);
            if (required <= 0) {
                return {};
            }
            std::wstring result(static_cast<size_t>(required), L'\0');
            const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, src.data(),
                                                    static_cast<int>(src.size()), result.data(), required);
            if (written <= 0) {
                return {};
            }
            return result;
#else
            return ConvertWithIconv<wchar_t>(src, "WCHAR_T", "UTF-8");
#endif
        }

        static inline std::string ToUTF8(const std::wstring& src) {
#ifdef WIN32
            if (src.empty()) {
                return {};
            }
            const int required = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src.data(),
                                                     static_cast<int>(src.size()), nullptr, 0, nullptr, nullptr);
            if (required <= 0) {
                return {};
            }
            std::string result(static_cast<size_t>(required), '\0');
            const int written = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, src.data(),
                                                    static_cast<int>(src.size()), result.data(), required, nullptr, nullptr);
            if (written <= 0) {
                return {};
            }
            return result;
#else
            const auto src_bytes = std::string_view(reinterpret_cast<const char*>(src.data()),
                                                    src.size() * sizeof(wchar_t));
            return ConvertWithIconv<char>(src_bytes, "UTF-8", "WCHAR_T");
#endif
        }

#ifdef WIN32
        static std::string GetErrorStr(HRESULT hr) {
            wchar_t buffer[4096] = { 0 };
            FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                           NULL, hr,
                           MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                           buffer, sizeof(buffer) / sizeof(*buffer), NULL);
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
            if (str.empty()) return false;
            for (char c : str) {
                if (!std::isdigit(c)) {
                    return false;
                }
            }
            return true;
        }

        static std::string ToHexString(const std::vector<uint8_t>& data);

    private:
#ifndef WIN32
        template <typename CharT>
        static std::basic_string<CharT> ConvertWithIconv(std::string_view src_bytes,
                                                         const char* to_code,
                                                         const char* from_code) {
            if (src_bytes.empty()) {
                return {};
            }

            iconv_t cd = iconv_open(to_code, from_code);
            if (cd == reinterpret_cast<iconv_t>(-1)) {
                return {};
            }

            std::basic_string<CharT> result;
            size_t out_capacity = std::max(static_cast<size_t>(16), src_bytes.size() + 1);
            result.resize(out_capacity);

            char* in_buf = const_cast<char*>(src_bytes.data());
            size_t in_bytes_left = src_bytes.size();
            char* out_buf = reinterpret_cast<char*>(result.data());
            size_t out_bytes_left = result.size() * sizeof(CharT);

            while (true) {
                const size_t iconv_result = iconv(cd, &in_buf, &in_bytes_left, &out_buf, &out_bytes_left);
                if (iconv_result != reinterpret_cast<size_t>(-1)) {
                    break;
                }

                if (errno != E2BIG) {
                    iconv_close(cd);
                    return {};
                }

                const auto used_bytes = result.size() * sizeof(CharT) - out_bytes_left;
                result.resize(result.size() * 2);
                out_buf = reinterpret_cast<char*>(result.data()) + used_bytes;
                out_bytes_left = result.size() * sizeof(CharT) - used_bytes;
            }

            const auto written_bytes = result.size() * sizeof(CharT) - out_bytes_left;
            result.resize(written_bytes / sizeof(CharT));
            iconv_close(cd);
            return result;
        }
#endif
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

    // U8Path: A wrapper around std::filesystem::path that prevents implicit
    // conversion from std::string (which would use ANSI codepage on Windows).
    // Always use this when constructing a path from a UTF-8 encoded std::string.
    class U8Path {
        std::filesystem::path path_;
    public:
        U8Path() = default;
        explicit U8Path(const std::string& s) : path_(PathFromUTF8(s)) {}
        explicit U8Path(const char* s) : path_(PathFromUTF8(s)) {}
        U8Path(const std::filesystem::path& p) : path_(p) {}
        U8Path(std::wstring s) : path_(std::move(s)) {}

        operator const std::filesystem::path&() const { return path_; }
        const std::filesystem::path& path() const { return path_; }

        const std::wstring wstring() const { return path_.wstring(); }
        std::string string() const { return StringUtil::ToUTF8(path_.wstring()); }

        U8Path parent_path() const { return path_.parent_path(); }
        U8Path filename() const { return path_.filename(); }
        bool empty() const { return path_.empty(); }

        // Note: Use .path() for path concatenation with std::filesystem::path operators.
    };

#if defined(__cpp_char8_t)
    inline const char* U8S(const char8_t* s) {
        return reinterpret_cast<const char*>(s);
    }
#else
    inline const char* U8S(const char* s) {
        return s;
    }
#endif

}

#endif //TC_APPLICATION_STRINGEXT_H
