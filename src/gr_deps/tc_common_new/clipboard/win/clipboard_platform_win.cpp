#include <cstring>
#include "tc_common_new/clipboard/clipboard_file_builder.h"
#include "tc_common_new/clipboard/win/clipboard_platform_win.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/log.h"
#include <Windows.h>
#include <shellapi.h>
#include <ole2.h>

namespace tc::clipboard
{
    namespace {

        bool EnsureOleInitialized() {
            static const bool ok = [] {
                const HRESULT hr = OleInitialize(nullptr);
                return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
            }();
            return ok;
        }

        class ClipboardScope {
        public:
            explicit ClipboardScope(HWND owner = nullptr) {
                opened_ = OpenClipboard(owner);
            }
            ~ClipboardScope() {
                if (opened_) {
                    CloseClipboard();
                }
            }
            bool Ok() const { return opened_; }
        private:
            bool opened_ = false;
        };

        std::string ReadUnicodeText(HANDLE data) {
            if (!data) {
                return "";
            }
            const auto* text = static_cast<const wchar_t*>(GlobalLock(data));
            if (!text) {
                return "";
            }
            const auto result = StringUtil::ToUTF8(text);
            GlobalUnlock(data);
            return result;
        }

        void ReadFileList(HDROP drop, std::vector<std::string>& paths) {
            if (!drop) {
                return;
            }
            const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
            paths.reserve(paths.size() + count);
            for (UINT i = 0; i < count; ++i) {
                const UINT len = DragQueryFileW(drop, i, nullptr, 0);
                if (len == 0) {
                    continue;
                }
                std::wstring path(len, L'\0');
                if (DragQueryFileW(drop, i, path.data(), len + 1) == 0) {
                    continue;
                }
                paths.push_back(StringUtil::ToUTF8(path.c_str()));
            }
        }

    }

    bool PlatformWin::Read(Content& out) {
        out = {};
        EnsureOleInitialized();
        for (int attempt = 0; attempt < 20; ++attempt) {
            ClipboardScope scope;
            if (!scope.Ok()) {
                Sleep(5);
                continue;
            }

            if (const HANDLE text_data = GetClipboardData(CF_UNICODETEXT)) {
                out.text_ = ReadUnicodeText(text_data);
            }

            if (const HANDLE drop_data = GetClipboardData(CF_HDROP)) {
                std::vector<std::string> paths;
                ReadFileList(static_cast<HDROP>(drop_data), paths);
                if (auto entries = BuildFileEntriesFromPaths(paths)) {
                    out.files_ = std::move(entries.value());
                }
            }

            return true;
        }
        return false;
    }

    bool PlatformWin::WriteText(const std::string& utf8_text) {
        EnsureOleInitialized();
        const auto wide = StringUtil::ToWString(utf8_text);
        if (wide.empty() && !utf8_text.empty()) {
            return false;
        }
        const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);

        for (int attempt = 0; attempt < 20; ++attempt) {
            HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (!memory) {
                return false;
            }

            void* locked = GlobalLock(memory);
            if (!locked) {
                GlobalFree(memory);
                return false;
            }
            memcpy(locked, wide.c_str(), bytes);
            GlobalUnlock(memory);

            ClipboardScope scope;
            if (!scope.Ok()) {
                GlobalFree(memory);
                Sleep(5);
                continue;
            }
            if (!EmptyClipboard()) {
                GlobalFree(memory);
                Sleep(5);
                continue;
            }
            if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
                Sleep(5);
                continue;
            }
            return true;
        }
        return false;
    }

    bool PlatformWin::Clear() {
        EnsureOleInitialized();
        const HRESULT release_ole = OleSetClipboard(nullptr);
        if (SUCCEEDED(release_ole)) {
            OleFlushClipboard();
        }
        for (int attempt = 0; attempt < 20; ++attempt) {
            ClipboardScope scope;
            if (!scope.Ok()) {
                Sleep(5);
                continue;
            }
            EmptyClipboard();
            return true;
        }
        return SUCCEEDED(release_ole);
    }

}
