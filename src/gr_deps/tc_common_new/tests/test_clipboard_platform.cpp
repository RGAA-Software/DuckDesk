//
// Unit tests for tc_common_new/clipboard/clipboard_platform.h
// Includes mock tests (all platforms) and Win32 integration tests (WIN32 only).
//

#include <gtest/gtest.h>
#include "tc_common_new/clipboard/clipboard_platform.h"
#include "tc_common_new/clipboard/clipboard_types.h"
#include "tc_common_new/clipboard/stub/clipboard_platform_stub.h"

#if defined(WIN32)
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include <mutex>
#endif

using namespace tc::clipboard;

namespace {

    class FakePlatform final : public IPlatform {
    public:
        bool Read(Content& out) override {
            if (!read_ok_) {
                return false;
            }
            out.text_ = text_;
            out.files_ = files_;
            return true;
        }

        bool WriteText(const std::string& utf8_text) override {
            ++write_calls_;
            if (fail_writes_remaining_ > 0) {
                --fail_writes_remaining_;
                return false;
            }
            text_ = utf8_text;
            return true;
        }

        bool Clear() override {
            ++clear_calls_;
            text_.clear();
            files_.clear();
            return clear_ok_;
        }

        std::string text_;
        std::vector<FileEntry> files_;
        bool read_ok_ = true;
        bool clear_ok_ = true;
        int write_calls_ = 0;
        int clear_calls_ = 0;
        int fail_writes_remaining_ = 0;
    };

#if defined(WIN32)
    std::mutex& ClipboardTestMutex() {
        static std::mutex mutex;
        return mutex;
    }

    class ClipboardTestLock {
    public:
        ClipboardTestLock() {
            ClipboardTestMutex().lock();
            Sleep(30);
        }
        ~ClipboardTestLock() {
            Sleep(30);
            ClipboardTestMutex().unlock();
        }
    };

    bool OpenClipboardWithRetry(HWND owner = nullptr, int attempts = 30) {
        for (int i = 0; i < attempts; ++i) {
            if (OpenClipboard(owner)) {
                return true;
            }
            Sleep(10);
        }
        return false;
    }

    class ClipboardBackup {
    public:
        ClipboardBackup() : platform_(CreatePlatform()) {
            Content content;
            if (platform_->Read(content)) {
                had_content_ = !content.Empty();
                backup_ = std::move(content);
            }
        }

        ~ClipboardBackup() {
            for (int i = 0; i < 20; ++i) {
                if (OpenClipboardWithRetry(nullptr, 10)) {
                    EmptyClipboard();
                    CloseClipboard();
                    break;
                }
                Sleep(10);
            }
            if (had_content_ && backup_.HasText()) {
                platform_->WriteText(backup_.text_);
            }
        }

        IPlatform& Platform() { return *platform_; }

    private:
        std::unique_ptr<IPlatform> platform_;
        Content backup_;
        bool had_content_ = false;
    };

    bool SetClipboardFileDrop(const std::vector<std::wstring>& files) {
        if (files.empty()) {
            return false;
        }

        size_t bytes = sizeof(DROPFILES);
        for (const auto& file : files) {
            bytes += (file.size() + 1) * sizeof(wchar_t);
        }
        bytes += sizeof(wchar_t);

        HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes);
        if (!memory) {
            return false;
        }

        auto* drop = static_cast<DROPFILES*>(GlobalLock(memory));
        if (!drop) {
            GlobalFree(memory);
            return false;
        }
        drop->pFiles = sizeof(DROPFILES);
        drop->fWide = TRUE;

        auto* dest = reinterpret_cast<wchar_t*>(reinterpret_cast<BYTE*>(drop) + sizeof(DROPFILES));
        for (const auto& file : files) {
            wcscpy_s(dest, file.size() + 1, file.c_str());
            dest += file.size() + 1;
        }
        *dest = L'\0';
        GlobalUnlock(memory);

        if (!OpenClipboardWithRetry(nullptr)) {
            GlobalFree(memory);
            return false;
        }
        EmptyClipboard();
        const HANDLE data = SetClipboardData(CF_HDROP, memory);
        CloseClipboard();
        return data != nullptr;
    }
#endif

}

TEST(ClipboardPlatformStubTest, StubReadWriteClear) {
    PlatformStub stub;
    Content content;
    EXPECT_FALSE(stub.Read(content));
    EXPECT_FALSE(stub.WriteText("hello"));
    EXPECT_FALSE(stub.Clear());
}

TEST(ClipboardCreatePlatformTest, FactoryNotNull) {
    auto platform = CreatePlatform();
    ASSERT_NE(platform, nullptr);
}

TEST(ClipboardWriteTextWithRetryTest, RejectsEmptyText) {
    FakePlatform fake;
    EXPECT_FALSE(WriteTextWithRetry(fake, "", 3, 0));
    EXPECT_EQ(fake.write_calls_, 0);
}

TEST(ClipboardWriteTextWithRetryTest, SucceedsWhenAlreadyPresent) {
    FakePlatform fake;
    fake.text_ = "cached";
    EXPECT_TRUE(WriteTextWithRetry(fake, "cached", 3, 0));
    EXPECT_EQ(fake.write_calls_, 0);
}

TEST(ClipboardWriteTextWithRetryTest, SucceedsAfterWrite) {
    FakePlatform fake;
    EXPECT_TRUE(WriteTextWithRetry(fake, "payload", 3, 0));
    EXPECT_GE(fake.write_calls_, 1);
    EXPECT_EQ(fake.text_, "payload");
}

TEST(ClipboardWriteTextWithRetryTest, RetriesUntilSuccess) {
    FakePlatform fake;
    fake.fail_writes_remaining_ = 2;
    EXPECT_TRUE(WriteTextWithRetry(fake, "retry-me", 5, 0));
    EXPECT_EQ(fake.write_calls_, 3);
    EXPECT_EQ(fake.text_, "retry-me");
}

TEST(ClipboardWriteTextWithRetryTest, FailsWhenExhausted) {
    FakePlatform fake;
    fake.fail_writes_remaining_ = 100;
    EXPECT_FALSE(WriteTextWithRetry(fake, "never", 3, 0));
    EXPECT_EQ(fake.write_calls_, 3);
}

TEST(ClipboardWriteTextWithRetryTest, FailsWhenReadFails) {
    FakePlatform fake;
    fake.read_ok_ = false;
    EXPECT_FALSE(WriteTextWithRetry(fake, "text", 3, 0));
}

#if defined(WIN32)

TEST(ClipboardPlatformWinTest, WriteReadRoundTripAscii) {
    ClipboardTestLock lock;
    ClipboardBackup backup;
    auto& platform = backup.Platform();

    ASSERT_TRUE(platform.WriteText("gamma-ray-clipboard-test"));
    Content content;
    ASSERT_TRUE(platform.Read(content));
    EXPECT_EQ(content.text_, "gamma-ray-clipboard-test");
}

TEST(ClipboardPlatformWinTest, WriteReadRoundTripUnicode) {
    ClipboardTestLock lock;
    ClipboardBackup backup;
    auto& platform = backup.Platform();

    const std::string unicode = "剪贴板测试 🎉 Γαμμα";
    ASSERT_TRUE(platform.WriteText(unicode));
    Content content;
    ASSERT_TRUE(platform.Read(content));
    EXPECT_EQ(content.text_, unicode);
}

TEST(ClipboardPlatformWinTest, WriteTextWithRetryOnRealClipboard) {
    ClipboardTestLock lock;
    ClipboardBackup backup;
    auto& platform = backup.Platform();

    EXPECT_TRUE(WriteTextWithRetry(platform, "retry-on-real-clipboard", 10, 5));
    Content content;
    ASSERT_TRUE(platform.Read(content));
    EXPECT_EQ(content.text_, "retry-on-real-clipboard");
}

TEST(ClipboardPlatformWinTest, ClearRemovesText) {
    ClipboardTestLock lock;
    ClipboardBackup backup;
    auto& platform = backup.Platform();

    ASSERT_TRUE(platform.WriteText("to-be-cleared"));
    ASSERT_TRUE(platform.Clear());
    Content content;
    ASSERT_TRUE(platform.Read(content));
    EXPECT_FALSE(content.HasText());
}

TEST(ClipboardPlatformWinTest, ReadFileDropFromClipboard) {
    ClipboardTestLock lock;
    ClipboardBackup backup;
    auto& platform = backup.Platform();

    const auto temp_dir = std::filesystem::temp_directory_path() / "tc_clipboard_platform_hdrop";
    std::filesystem::remove_all(temp_dir);
    std::filesystem::create_directories(temp_dir / "nested");
    {
        std::ofstream(temp_dir / "clip-a.txt") << "aaa";
        std::ofstream(temp_dir / "nested" / "clip-b.txt") << "bbbb";
    }

    const std::wstring wdir = temp_dir.wstring();
    const std::wstring f1 = (temp_dir / "clip-a.txt").wstring();
    const std::wstring f2 = (temp_dir / "nested" / "clip-b.txt").wstring();
    ASSERT_TRUE(SetClipboardFileDrop({f1, f2}));

    Content content;
    ASSERT_TRUE(platform.Read(content));
    EXPECT_TRUE(content.HasFiles());
    EXPECT_GE(content.files_.size(), 2u);

    bool found_a = false;
    bool found_b = false;
    for (const auto& file : content.files_) {
        if (file.file_name_ == "clip-a.txt") {
            found_a = true;
            EXPECT_EQ(file.total_size_, 3);
        }
        if (file.file_name_ == "clip-b.txt") {
            found_b = true;
            EXPECT_EQ(file.total_size_, 4);
        }
    }
    EXPECT_TRUE(found_a);
    EXPECT_TRUE(found_b);

    std::filesystem::remove_all(temp_dir);
}

TEST(ClipboardPlatformWinTest, MultilineTextPreserved) {
    ClipboardTestLock lock;
    ClipboardBackup backup;
    auto& platform = backup.Platform();

    const std::string multiline = "line1\r\nline2\nline3";
    ASSERT_TRUE(platform.WriteText(multiline));
    Content content;
    ASSERT_TRUE(platform.Read(content));
    EXPECT_EQ(content.text_, multiline);
}

#endif
