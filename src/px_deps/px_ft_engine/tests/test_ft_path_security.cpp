// 路径安全校验单测 - 移植自 rustdesk/libs/hbb_common/src/fs.rs:1529-1807 的 #[test] 用例
#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>

#ifdef _WIN32
#include <process.h>
#endif

#include "ft_path.h"
#include "transfer_job.h"

namespace px::ft {
namespace {

// fs.rs tests: TestTempDir
class TestTempDir {
public:
    explicit TestTempDir(const std::string& prefix) {
        auto ts = std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::system_clock::now().time_since_epoch())
                      .count();
        static int counter = 0;
#ifdef _WIN32
        int pid = _getpid();
#else
        int pid = static_cast<int>(::getpid());
#endif
        path_ = std::filesystem::temp_directory_path() /
                (prefix + "_" + std::to_string(pid) + "_" +
                 std::to_string(ts) + "_" + std::to_string(counter++));
        std::filesystem::create_directories(path_);
    }
    ~TestTempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path_, ec);
    }
    std::filesystem::path join(const std::string& p) const { return path_ / ToFsPath(p); }
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

px::FileEntry NewFileEntry(const std::string& name) {
    px::FileEntry e;
    e.set_name(name);
    return e;
}

// fs.rs tests: new_validation_job
TransferJob NewValidationJob(int32_t id) {
    return TransferJob::NewWrite(id, JobType::Generic, "/fake/remote",
                                 DataSource{std::filesystem::temp_directory_path() /
                                            ("rustdesk_validation_" + std::to_string(id))},
                                 0, false, true, false);
}

// fs.rs tests: new_write_job(带文件列表,触发 SetFiles 校验)
TransferJob NewWriteJob(int32_t id, const std::filesystem::path& download_dir,
                        const std::string& name) {
    TransferJob job = TransferJob::NewWrite(id, JobType::Generic, "/fake/remote",
                                          DataSource{download_dir}, 0, false, true, false);
    job.SetFiles({NewFileEntry(name)});
    return job;
}

void ExpectErrContains(const std::function<void()>& fn, const std::string& expected) {
    try {
        fn();
        FAIL() << "expected error containing '" << expected << "', but no exception was thrown";
    } catch (const std::exception& e) {
        EXPECT_NE(std::string(e.what()).find(expected), std::string::npos)
            << "expected error containing '" << expected << "', got: " << e.what();
    }
}

// fs.rs:1607 path_traversal_e2e_write_rejects_relative_escape
TEST(PathSecurity, WriteRejectsRelativeEscape) {
    TestTempDir tmp("rustdesk_e2e_relative");
    auto downloads = tmp.join("downloads");
    std::filesystem::create_directories(downloads);
    ExpectErrContains([&] { NewWriteJob(1, downloads, "../traversal_proof.txt"); },
                      "path traversal");
    EXPECT_FALSE(std::filesystem::exists(tmp.join("traversal_proof.txt")));
}

// fs.rs:1619 path_traversal_e2e_write_rejects_absolute_path
TEST(PathSecurity, WriteRejectsAbsolutePath) {
    TestTempDir tmp("rustdesk_e2e_absolute");
    auto downloads = tmp.join("downloads");
    auto absolute_target = tmp.join("fake_ssh") / "authorized_keys";
    std::filesystem::create_directories(downloads);
    ExpectErrContains([&] { NewWriteJob(2, downloads, ToUtf8(absolute_target)); },
                      "absolute path");
    EXPECT_FALSE(std::filesystem::exists(absolute_target));
}

// fs.rs:1632 path_traversal_e2e_write_rejects_symlink_escape
// Windows 下创建符号链接需要权限(上游同样 ignore);创建不了就跳过
TEST(PathSecurity, WriteRejectsSymlinkEscape) {
    TestTempDir tmp("rustdesk_e2e_symlink");
    auto downloads = tmp.join("downloads");
    auto outside = tmp.join("outside");
    auto escaped_target = outside / "escape.txt";
    std::filesystem::create_directories(downloads);
    std::filesystem::create_directories(outside);
    auto symlink_path = downloads / "link";
    std::error_code ec;
    std::filesystem::create_directory_symlink(outside, symlink_path, ec);
    if (ec) GTEST_SKIP() << "requires symlink privilege to create test symlink";
    ExpectErrContains([&] { NewWriteJob(3, downloads, "link/escape.txt"); }, "symlink");
    EXPECT_FALSE(std::filesystem::exists(escaped_target));
}

// fs.rs:1660 set_files_allows_single_empty_name_for_single_file_transfer
TEST(PathSecurity, SetFilesAllowsSingleEmptyName) {
    auto job = NewValidationJob(101);
    EXPECT_NO_THROW(job.SetFiles({NewFileEntry("")}));
}

// fs.rs:1666 set_files_rejects_empty_name_in_multi_file_transfer
TEST(PathSecurity, SetFilesRejectsEmptyNameInMultiFile) {
    auto job = NewValidationJob(102);
    ExpectErrContains([&] { job.SetFiles({NewFileEntry(""), NewFileEntry("ok.txt")}); },
                      "empty file name");
}

// fs.rs:1675 set_files_rejects_null_byte_name
TEST(PathSecurity, SetFilesRejectsNullByteName) {
    auto job = NewValidationJob(103);
    ExpectErrContains([&] { job.SetFiles({NewFileEntry(std::string("bad\0name.txt", 13))}); },
                      "null bytes");
}

// fs.rs:1684 set_files_rejects_mixed_entries_when_one_is_traversal
TEST(PathSecurity, SetFilesRejectsMixedEntriesWhenOneIsTraversal) {
    auto job = NewValidationJob(104);
    ExpectErrContains(
        [&] { job.SetFiles({NewFileEntry("safe/file.txt"), NewFileEntry("../../escape.txt")}); },
        "path traversal");
}

#ifdef _WIN32
// fs.rs:1696 set_files_rejects_unc_absolute_path
TEST(PathSecurity, SetFilesRejectsUncAbsolutePath) {
    auto job = NewValidationJob(105);
    ExpectErrContains([&] { job.SetFiles({NewFileEntry("\\\\server\\share\\payload.txt")}); },
                      "absolute path");
}

// fs.rs:1790 set_files_rejects_windows_drive_absolute_path
TEST(PathSecurity, SetFilesRejectsWindowsDriveAbsolutePath) {
    auto job = NewValidationJob(106);
    ExpectErrContains([&] { job.SetFiles({NewFileEntry("C:\\Windows\\Temp\\payload.txt")}); },
                      "absolute path");
}

// fs.rs:1799 set_files_rejects_windows_verbatim_drive_absolute_path
TEST(PathSecurity, SetFilesRejectsWindowsVerbatimDriveAbsolutePath) {
    auto job = NewValidationJob(1061);
    ExpectErrContains([&] { job.SetFiles({NewFileEntry(R"(\\?\C:\Windows\Temp\x.txt)")}); },
                      "absolute path");
}
#endif

// fs.rs:1715 remove_file_rejects_empty_path
TEST(PathSecurity, RemoveFileRejectsEmptyPath) {
    ExpectErrContains([&] { RemoveFile(""); }, "cannot be empty");
}

// fs.rs:1721 remove_file_rejects_null_byte_path
TEST(PathSecurity, RemoveFileRejectsNullBytePath) {
    ExpectErrContains([&] { RemoveFile(std::string("bad\0path", 8)); }, "null bytes");
}

// fs.rs:1727 create_dir_rejects_empty_path
TEST(PathSecurity, CreateDirRejectsEmptyPath) {
    ExpectErrContains([&] { CreateDir(""); }, "cannot be empty");
}

// fs.rs:1733 create_dir_rejects_null_byte_path
TEST(PathSecurity, CreateDirRejectsNullBytePath) {
    ExpectErrContains([&] { CreateDir(std::string("bad\0path", 8)); }, "null bytes");
}

// fs.rs:1739 rename_file_rejects_invalid_new_name
TEST(PathSecurity, RenameFileRejectsInvalidNewName) {
    TestTempDir tmp("rustdesk_rename_invalid");
    auto src = tmp.join("source.txt");
    { std::ofstream ofs(src, std::ios::binary); ofs << "content"; }
    std::string src_str = ToUtf8(src);

    ExpectErrContains([&] { RenameFile(src_str, ""); }, "cannot be empty");
    ExpectErrContains([&] { RenameFile(src_str, "../escape.txt"); }, "path traversal");
    ExpectErrContains([&] { RenameFile(src_str, std::string("bad\0name.txt", 13)); },
                      "null bytes");
#ifdef _WIN32
    ExpectErrContains([&] { RenameFile(src_str, "C:\\Windows\\Temp\\payload.txt"); },
                      "absolute path");
#else
    ExpectErrContains([&] { RenameFile(src_str, "/tmp/payload.txt"); }, "absolute path");
#endif
}

// fs.rs:1774 rename_file_accepts_valid_new_name
TEST(PathSecurity, RenameFileAcceptsValidNewName) {
    TestTempDir tmp("rustdesk_rename_ok");
    auto src = tmp.join("rename_src.txt");
    auto dst = tmp.join("renamed.txt");
    { std::ofstream ofs(src, std::ios::binary); ofs << "content"; }
    EXPECT_NO_THROW(RenameFile(ToUtf8(src), "renamed.txt"));
    EXPECT_FALSE(std::filesystem::exists(src));
    EXPECT_TRUE(std::filesystem::exists(dst));
}

// 补充:JoinValidatedPath 合法名正常工作
TEST(PathSecurity, JoinValidatedPathAcceptsNormalName) {
    TestTempDir tmp("rustdesk_join_ok");
    auto downloads = tmp.join("downloads");
    std::filesystem::create_directories(downloads);
    auto p = JoinValidatedPath(downloads, "sub/dir/file.txt");
    EXPECT_EQ(p, downloads / ToFsPath("sub/dir/file.txt"));
}

// 补充:盘符列目录(ReadDir "/")
#ifdef _WIN32
TEST(PathSecurity, ReadDirRootListsDrives) {
    auto fd = ReadDir("/", false);
    bool has_drive = false;
    for (const auto& e : fd.entries()) {
        // 盘符项:name 为 Shell 显示名(带卷标),导航用 abs_path("C:/" 形态)
        if (e.entry_type() == px::FileType::DirDrive && e.abs_path().size() == 3 &&
            e.abs_path()[1] == ':' && e.abs_path()[2] == '/') {
            has_drive = true;
        }
    }
    EXPECT_TRUE(has_drive);
}

// "C:" 盘符相对路径必须归一为盘根 "C:/",否则会被解析成进程 CWD
TEST(PathSecurity, ReadDirDriveLetterNormalizesToRoot) {
    const char* sysdrive = getenv("SYSTEMDRIVE"); // 通常 "C:"
    ASSERT_NE(sysdrive, nullptr);
    auto fd = ReadDir(sysdrive, false);
    EXPECT_EQ(fd.path(), std::string(sysdrive) + "/");
    EXPECT_GT(fd.entries_size(), 0); // 系统盘根目录必有内容
}

// 根视图应追加常用文件夹(用户目录/桌面等),携带 abs_path 供导航
TEST(PathSecurity, ReadDirRootIncludesPinnedFolders) {
    auto fd = ReadDir("/", false);
    bool has_pinned = false;
    for (const auto& e : fd.entries()) {
        if (e.entry_type() == px::FileType::Dir && !e.abs_path().empty() &&
            !e.name().empty()) {
            has_pinned = true;
            // abs_path 必须真实存在且为目录
            EXPECT_TRUE(std::filesystem::is_directory(ToFsPath(e.abs_path())));
        }
    }
    EXPECT_TRUE(has_pinned); // USERPROFILE 至少有一个
}
#endif

} // namespace
} // namespace px::ft
