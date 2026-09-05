//
// Unit tests for px_common_new/clipboard/clipboard_platform.h
// Includes mock tests (all platforms) and Win32 integration tests (WIN32 only).
//

#include <gtest/gtest.h>
#include "px_common_new/clipboard/clipboard_platform.h"
#include "px_common_new/clipboard/clipboard_types.h"
#include "px_common_new/clipboard/stub/clipboard_platform_stub.h"

#if defined(WIN32)
#include <Windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <span>
#include <type_traits>
#endif

using namespace px::clipboard;

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

struct GlobalMemoryDeleter final {
    void operator()(std::remove_pointer_t<HGLOBAL>* memory) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): Win32 HGLOBAL ABI.
        if (memory != nullptr) {
            GlobalFree(memory);
        }
    }
};

struct GlobalUnlockDeleter final {
    HGLOBAL memory_{};

    void operator()(void*) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): transient GlobalLock address.
        if (memory_ != nullptr) {
            GlobalUnlock(memory_);
        }
    }
};

using UniqueGlobalMemory = std::unique_ptr<std::remove_pointer_t<HGLOBAL>, GlobalMemoryDeleter>;
using UniqueGlobalLock = std::unique_ptr<void, GlobalUnlockDeleter>;

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

    IPlatform& Platform() {
        return *platform_;
    }

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

    UniqueGlobalMemory memory{GlobalAlloc(GMEM_MOVEABLE | GMEM_ZEROINIT, bytes)};
    if (!memory) {
        return false;
    }

    {
        const UniqueGlobalLock lock{GlobalLock(memory.get()), GlobalUnlockDeleter{memory.get()}};
        if (!lock) {
            return false;
        }
        auto payload = std::span<std::byte>{static_cast<std::byte*>(lock.get()), bytes}; // NOLINT(gammaray-raw-pointer-boundary)
        DROPFILES drop{};
        drop.pFiles = sizeof(DROPFILES);
        drop.fWide = TRUE;
        std::memcpy(payload.data(), std::addressof(drop), sizeof(drop));

        std::size_t offset = sizeof(drop);
        for (const auto& file : files) {
            const auto file_bytes = (file.size() + 1) * sizeof(wchar_t);
            std::memcpy(payload.data() + offset, file.c_str(), file_bytes);
            offset += file_bytes;
        }
        std::memset(payload.data() + offset, 0, sizeof(wchar_t));
    }

    if (!OpenClipboardWithRetry(nullptr)) {
        return false;
    }
    EmptyClipboard();
    const HANDLE data = SetClipboardData(CF_HDROP, memory.get());
    if (data != nullptr) {
        static_cast<void>(memory.release()); // NOLINT(gammaray-raw-pointer-boundary): ownership transferred to the OS clipboard.
    }
    CloseClipboard();
    return data != nullptr;
}
#endif

} // namespace

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

    const auto temp_dir = std::filesystem::temp_directory_path() / "px_clipboard_platform_hdrop";
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
