#include "rdp_windows_clipboard.h"

#include <ShlObj.h>
#include <Windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <vector>

#include "rdp_clipboard_files.h"

namespace px::rdp {
namespace {
constexpr std::size_t kMaximumClipboardBytes{16U * 1024U * 1024U};

class ClipboardScope final {
public:
    ClipboardScope() : opened_{OpenClipboard(nullptr) != FALSE} {}  // NOLINT(pixels-raw-pointer-boundary): synchronous Win32 owner boundary.
    ~ClipboardScope() {
        if (opened_) CloseClipboard();
    }
    [[nodiscard]] bool IsOpen() const noexcept { return opened_; }

private:
    bool opened_{};
};

struct GlobalMemoryCloser final {
    void operator()(std::remove_pointer_t<HGLOBAL>* memory) const noexcept {  // NOLINT(pixels-raw-pointer-boundary): Win32 allocation ABI.
        if (memory) GlobalFree(memory);
    }
};
using GlobalMemory = std::unique_ptr<std::remove_pointer_t<HGLOBAL>, GlobalMemoryCloser>;

std::vector<std::uint8_t> ReadBytes(const UINT format) {
    const HANDLE memory = GetClipboardData(format);  // NOLINT(pixels-raw-pointer-boundary): borrowed Win32 clipboard handle.
    if (!memory) return {};
    const SIZE_T size = GlobalSize(memory);
    if (size == 0 || size > kMaximumClipboardBytes) return {};
    const auto locked = static_cast<const std::uint8_t*>(GlobalLock(memory));  // NOLINT(pixels-raw-pointer-boundary): scoped locked view.
    if (!locked) return {};
    std::vector<std::uint8_t> bytes(locked, locked + size);
    GlobalUnlock(memory);
    return bytes;
}

std::string ReadText() {
    const auto bytes = ReadBytes(CF_UNICODETEXT);
    if (bytes.size() < sizeof(wchar_t) || bytes.size() % sizeof(wchar_t) != 0) return {};
    const auto text = std::span<const wchar_t>{reinterpret_cast<const wchar_t*>(bytes.data()), bytes.size() / sizeof(wchar_t)};
    const auto terminator = std::ranges::find(text, L'\0');
    const int characters = static_cast<int>(std::distance(text.begin(), terminator));
    if (characters <= 0) return {};
    const int byteCount = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), characters, nullptr, 0, nullptr, nullptr);
    if (byteCount <= 0 || static_cast<std::size_t>(byteCount) > kMaximumClipboardBytes) return {};
    std::string result(static_cast<std::size_t>(byteCount), '\0');
    return WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(), characters, result.data(), byteCount, nullptr, nullptr) == byteCount
               ? result
               : std::string{};
}

std::vector<std::uint8_t> EncodeText(const std::string& text) {
    if (text.empty() || text.size() > kMaximumClipboardBytes) return {};
    const int characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (characters <= 0 || static_cast<std::size_t>(characters) >= kMaximumClipboardBytes / sizeof(wchar_t)) return {};
    std::vector<std::uint8_t> result((static_cast<std::size_t>(characters) + 1U) * sizeof(wchar_t));
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), reinterpret_cast<wchar_t*>(result.data()),
                               characters) == characters
               ? result
               : std::vector<std::uint8_t>{};
}

bool WriteBytes(const UINT format, const std::span<const std::uint8_t> bytes) {
    if (bytes.empty() || bytes.size() > kMaximumClipboardBytes) return false;
    GlobalMemory memory{GlobalAlloc(GMEM_MOVEABLE, bytes.size())};
    if (!memory) return false;
    void* locked = GlobalLock(memory.get());  // NOLINT(pixels-raw-pointer-boundary): scoped locked view.
    if (!locked) return false;
    std::memcpy(locked, bytes.data(), bytes.size());
    GlobalUnlock(memory.get());
    if (!SetClipboardData(format, memory.get())) return false;
    static_cast<void>(memory.release());  // NOLINT(pixels-raw-pointer-boundary): ownership transferred to Windows clipboard.
    return true;
}

std::vector<std::filesystem::path> ReadFiles() {
    const HANDLE clipboardData = GetClipboardData(CF_HDROP);  // NOLINT(pixels-raw-pointer-boundary): borrowed Win32 clipboard handle.
    if (!clipboardData) return {};
    const auto drop = static_cast<HDROP>(clipboardData);  // NOLINT(pixels-raw-pointer-boundary): scoped Win32 clipboard view.
    const UINT count = DragQueryFileW(drop, 0xFFFFFFFFU, nullptr, 0);
    if (count == 0 || count > kMaximumClipboardFileCount) return {};
    std::vector<std::filesystem::path> files{};
    files.reserve(count);
    for (UINT index{}; index < count; ++index) {
        const UINT characters = DragQueryFileW(drop, index, nullptr, 0);
        if (characters == 0 || characters >= 32767U) return {};
        std::wstring path(static_cast<std::size_t>(characters) + 1U, L'\0');
        if (DragQueryFileW(drop, index, path.data(), static_cast<UINT>(path.size())) != characters) return {};
        path.resize(characters);
        files.emplace_back(std::move(path));
    }
    return files;
}

bool WriteFiles(const std::span<const std::filesystem::path> files) {
    if (files.empty() || files.size() > kMaximumClipboardFileCount) return false;
    std::size_t characterCount{1U};
    for (const auto& file : files) {
        if (!file.is_absolute() || file.native().empty() || file.native().size() >= 32767U) return false;
        characterCount += file.native().size() + 1U;
    }
    const std::size_t byteCount = sizeof(DROPFILES) + characterCount * sizeof(wchar_t);
    GlobalMemory memory{GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, byteCount)};
    if (!memory) return false;
    void* locked = GlobalLock(memory.get());  // NOLINT(pixels-raw-pointer-boundary): scoped locked Win32 allocation.
    if (!locked) return false;
    auto& header = *static_cast<DROPFILES*>(locked);  // NOLINT(pixels-raw-pointer-boundary): scoped Win32 structure view.
    header.pFiles = sizeof(DROPFILES);
    header.fWide = TRUE;
    auto destination = reinterpret_cast<wchar_t*>(static_cast<std::uint8_t*>(locked) + sizeof(DROPFILES));  // NOLINT(pixels-raw-pointer-boundary)
    for (const auto& file : files) {
        const auto& value = file.native();
        std::copy(value.begin(), value.end(), destination);
        destination += value.size() + 1U;
    }
    GlobalUnlock(memory.get());
    if (!SetClipboardData(CF_HDROP, memory.get())) return false;
    static_cast<void>(memory.release());  // NOLINT(pixels-raw-pointer-boundary): ownership transferred to Windows clipboard.
    return true;
}
}  // namespace

std::optional<ClipboardContent> WindowsClipboard::ReadChanged() {
    const DWORD sequence = GetClipboardSequenceNumber();
    if (sequence != 0 && sequence == sequence_) return std::nullopt;

    ClipboardScope clipboard{};
    if (!clipboard.IsOpen()) return std::nullopt;

    ClipboardContent content{};
    if (IsClipboardFormatAvailable(CF_UNICODETEXT)) content.text = ReadText();
    const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (htmlFormat != 0 && IsClipboardFormatAvailable(htmlFormat)) content.html = ReadBytes(htmlFormat);
    if (IsClipboardFormatAvailable(CF_DIB)) content.dib = ReadBytes(CF_DIB);
    if (IsClipboardFormatAvailable(CF_DIBV5)) content.dibV5 = ReadBytes(CF_DIBV5);
    if (pngFormat != 0 && IsClipboardFormatAvailable(pngFormat)) content.png = ReadBytes(pngFormat);
    if (IsClipboardFormatAvailable(CF_HDROP)) content.files = ReadFiles();
    staging_.reset();
    sequence_ = sequence;
    return content;
}

bool WindowsClipboard::Write(const ClipboardContent& content) {
    ClipboardScope clipboard{};
    if (!clipboard.IsOpen() || !EmptyClipboard()) return false;

    bool success{true};
    if (!content.text.empty()) success = WriteBytes(CF_UNICODETEXT, EncodeText(content.text)) && success;
    const UINT htmlFormat = RegisterClipboardFormatW(L"HTML Format");
    const UINT pngFormat = RegisterClipboardFormatW(L"PNG");
    if (!content.html.empty()) success = htmlFormat != 0 && WriteBytes(htmlFormat, content.html) && success;
    if (!content.dib.empty()) success = WriteBytes(CF_DIB, content.dib) && success;
    if (!content.dibV5.empty()) success = WriteBytes(CF_DIBV5, content.dibV5) && success;
    if (!content.png.empty()) success = pngFormat != 0 && WriteBytes(pngFormat, content.png) && success;
    if (!content.files.empty()) success = WriteFiles(content.files) && success;
    if (success) {
        staging_ = content.staging;
        sequence_ = GetClipboardSequenceNumber();
    }
    return success;
}

}  // namespace px::rdp
