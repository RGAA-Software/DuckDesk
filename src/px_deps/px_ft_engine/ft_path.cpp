#include "ft_path.h"

#include <chrono>
#include <stdexcept>
#include <system_error>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

namespace px::ft {

namespace {

[[noreturn]] void Bail(const std::string& msg) { throw std::runtime_error(msg); }

// fs.rs:121 get_file_name
std::string GetFileName(const std::filesystem::path& p) { return ToUtf8(p.filename()); }

// fs.rs:143 read_dir_recursive
void ReadDirRecursive(const std::filesystem::path& path, const std::filesystem::path& prefix,
                      bool include_hidden, std::vector<px::FileEntry>* out) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        px::FileDirectory fd = ReadDir(ToUtf8(path), include_hidden);
        for (const auto& entry : fd.entries()) {
            if (entry.entry_type() == px::FileType::File) {
                px::FileEntry e = entry;
                e.set_name(ToUtf8(prefix / ToFsPath(entry.name())));
                out->push_back(std::move(e));
            } else if (entry.entry_type() == px::FileType::Dir) {
                ReadDirRecursive(path / ToFsPath(entry.name()), prefix / ToFsPath(entry.name()),
                                 include_hidden, out);
            }
            // 其余类型(DirLink/FileLink/DirDrive)跳过,与 fs.rs 一致
        }
    } else if (std::filesystem::is_regular_file(path, ec)) {
        px::FileEntry e;
        e.set_entry_type(px::FileType::File);
        e.set_size(std::filesystem::file_size(path, ec));
        if (ec) e.set_size(0);
        e.set_modified_time(GetFileMtimeSecs(path));
        // name 留空:单文件传输场景(fs.rs:190)
        out->push_back(std::move(e));
    } else {
        Bail("Not exists");
    }
}

// fs.rs:206 read_empty_dirs_recursive
void ReadEmptyDirsRecursive(const std::filesystem::path& path, const std::filesystem::path& prefix,
                            bool include_hidden, std::vector<px::FileDirectory>* out) {
    std::error_code ec;
    if (std::filesystem::is_directory(path, ec)) {
        px::FileDirectory fd = ReadDir(ToUtf8(path), include_hidden);
        if (fd.entries().empty()) {
            fd.set_path(ToUtf8(prefix.empty() ? path : prefix));
            out->push_back(std::move(fd));
        } else {
            for (const auto& entry : fd.entries()) {
                if (entry.entry_type() == px::FileType::Dir) {
                    ReadEmptyDirsRecursive(path / ToFsPath(entry.name()),
                                           prefix / ToFsPath(entry.name()), include_hidden, out);
                }
            }
        }
    } else if (std::filesystem::is_regular_file(path, ec)) {
        // 文件不产生空目录,直接返回
    } else {
        Bail("Not exists");
    }
}

} // namespace

std::filesystem::path ToFsPath(const std::string& utf8) {
#ifdef _WIN32
    // UTF-8 -> UTF-16,避免 ANSI 代码页丢字符
    std::u8string u8(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return std::filesystem::path(u8);
#else
    return std::filesystem::path(utf8);
#endif
}

std::string ToUtf8(const std::filesystem::path& p) {
#ifdef _WIN32
    std::u8string u8 = p.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
#else
    return p.string();
#endif
}

uint64_t GetFileMtimeSecs(const std::filesystem::path& p) {
    std::error_code ec;
    auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
    auto secs = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
    return secs > 0 ? static_cast<uint64_t>(secs) : 0;
}

bool SetFileMtimeSecs(const std::filesystem::path& p, uint64_t unix_secs) {
    auto sys = std::chrono::system_clock::time_point(std::chrono::seconds(unix_secs));
    auto ft = std::chrono::clock_cast<std::filesystem::file_time_type::clock>(sys);
    std::error_code ec;
    std::filesystem::last_write_time(p, ft, ec);
    return !ec;
}

std::string GetHomeAsString() {
#ifdef _WIN32
    if (const char* p = getenv("USERPROFILE")) return p;
    const char* drive = getenv("HOMEDRIVE");
    const char* path = getenv("HOMEPATH");
    if (drive && path) return std::string(drive) + path;
    return "C:\\";
#else
    if (const char* p = getenv("HOME")) return p;
    return "/";
#endif
}

px::FileDirectory ReadDir(const std::string& path, bool include_hidden) {
    px::FileDirectory dir;
    dir.set_path(path);
#ifdef _WIN32
    // fs.rs:41 - Windows 下 "/" 列盘符
    if (path == "/") {
        DWORD drives = GetLogicalDrives();
        for (int i = 0; i < 32; ++i) {
            if (drives & (1u << i)) {
                px::FileEntry* e = dir.add_entries();
                e->set_name(std::string(1, static_cast<char>('A' + i)) + ":");
                e->set_entry_type(px::FileType::DirDrive);
            }
        }
        return dir;
    }
#endif
    std::filesystem::path fs_path = ToFsPath(path);
    std::error_code ec;
    std::filesystem::directory_iterator it(
        fs_path, std::filesystem::directory_options::skip_permission_denied, ec);
    if (ec) Bail(ec.message());
    for (const auto& entry : it) {
        const std::filesystem::path& p = entry.path();
        std::string name = GetFileName(p);
        if (name.empty()) continue;

        // fs.rs:70 - symlink_metadata(不跟随链接)
        std::error_code sec;
        auto meta = std::filesystem::symlink_status(p, sec);
        if (sec) continue;

        bool is_hidden = false;
#ifdef _WIN32
        // fs.rs:77 - FILE_ATTRIBUTE_HIDDEN (0x2)
        DWORD attr = GetFileAttributesW(p.c_str());
        if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_HIDDEN)) is_hidden = true;
#else
        if (!name.empty() && name.front() == '.') is_hidden = true;
#endif
        if (is_hidden && !include_hidden) continue;

        px::FileEntry* e = dir.add_entries();
        e->set_name(name);
        e->set_is_hidden(is_hidden);
        bool is_symlink = meta.type() == std::filesystem::file_type::symlink;
        bool is_dir = std::filesystem::is_directory(p, sec); // 跟随链接,与 fs.rs:88 p.is_dir() 一致
        if (is_dir) {
            e->set_entry_type(is_symlink ? px::FileType::DirLink : px::FileType::Dir);
        } else if (is_symlink) {
            e->set_entry_type(px::FileType::FileLink);
        } else {
            e->set_entry_type(px::FileType::File);
            e->set_size(std::filesystem::file_size(p, sec));
            if (sec) e->set_size(0);
        }
        e->set_modified_time(GetFileMtimeSecs(p));
    }
    return dir;
}

std::vector<px::FileEntry> GetRecursiveFiles(const std::string& path, bool include_hidden) {
    std::vector<px::FileEntry> files;
    ReadDirRecursive(ToFsPath(path), std::filesystem::path(), include_hidden, &files);
    return files;
}

std::vector<px::FileDirectory> GetEmptyDirsRecursive(const std::string& path, bool include_hidden) {
    std::vector<px::FileDirectory> dirs;
    ReadEmptyDirsRecursive(ToFsPath(path), std::filesystem::path(), include_hidden, &dirs);
    return dirs;
}

bool IsFileExists(const std::string& file_path) {
    std::error_code ec;
    return std::filesystem::exists(ToFsPath(file_path), ec);
}

void CreateDir(const std::string& dir) {
    ValidateFsPathArgument(dir, "directory path");
    std::error_code ec;
    std::filesystem::create_directories(ToFsPath(dir), ec);
    if (ec) Bail(ec.message());
}

void RemoveFile(const std::string& file) {
    ValidateFsPathArgument(file, "file path");
    std::error_code ec;
    if (!std::filesystem::remove(ToFsPath(file), ec) || ec) {
        Bail(ec ? ec.message() : "file not exists");
    }
}

void RemoveAllEmptyDir(const std::filesystem::path& path) {
    px::FileDirectory fd = ReadDir(ToUtf8(path), true);
    for (const auto& entry : fd.entries()) {
        std::filesystem::path child = path / ToFsPath(entry.name());
        if (entry.entry_type() == px::FileType::Dir) {
            std::error_code ec;
            RemoveAllEmptyDir(child); // fs.rs 里 .ok() 忽略错误;此处递归失败同样向上抛,由边界兜底
        } else if (entry.entry_type() == px::FileType::DirLink ||
                   entry.entry_type() == px::FileType::FileLink) {
            std::error_code ec;
            std::filesystem::remove(child, ec);
        }
    }
    std::error_code ec;
    std::filesystem::remove(path, ec); // 只删空目录,与 fs.rs:1397 remove_dir 一致
}

void RenameFile(const std::string& path, const std::string& new_name) {
    ValidateFsPathArgument(path, "path");
    if (new_name.empty()) Bail("new file name cannot be empty");
    ValidateFileNameNoTraversal(new_name);
    std::filesystem::path p = ToFsPath(path);
    std::error_code ec;
    if (std::filesystem::exists(p, ec)) {
        std::filesystem::path parent = p.parent_path();
        if (parent.empty()) Bail("Parent directory of path not exists");
        std::filesystem::rename(p, parent / ToFsPath(new_name), ec);
        if (ec) Bail(ec.message());
    } else {
        Bail("path not exists");
    }
}

void TransformWindowsPath(std::vector<px::FileEntry>& entries) {
    for (auto& entry : entries) {
        std::string name = entry.name();
        for (auto& c : name) {
            if (c == '\\') c = '/';
        }
        entry.set_name(std::move(name));
    }
}

void ValidateFileNameNoTraversal(const std::string& name) {
    if (name.find('\0') != std::string::npos) {
        Bail("file name contains null bytes");
    }
    // fs.rs:464 - 按 '/' 与(Windows 下)'\\' 切分,拒绝任何 ".." 段
    size_t start = 0;
    auto check_segment = [&](std::string_view seg) {
        if (!seg.empty() && seg == "..") Bail("path traversal detected in file name");
    };
    for (size_t i = 0; i <= name.size(); ++i) {
        bool is_sep = i == name.size() || name[i] == '/'
#ifdef _WIN32
                      || name[i] == '\\'
#endif
            ;
        if (is_sep) {
            check_segment(std::string_view(name).substr(start, i - start));
            start = i + 1;
        }
    }
#ifdef _WIN32
    // fs.rs:472 - 盘符绝对路径 / 以 '/' 或 '\\' 开头(含 UNC 与 \\?\ 前缀)
    if (name.size() >= 2) {
        unsigned char c0 = static_cast<unsigned char>(name[0]);
        if (std::isalpha(c0) && name[1] == ':') {
            Bail("absolute path detected in file name");
        }
    }
    if (!name.empty() && (name.front() == '/' || name.front() == '\\')) {
        Bail("absolute path detected in file name");
    }
#else
    if (!name.empty() && name.front() == '/') {
        Bail("absolute path detected in file name");
    }
#endif
}

void ValidateTransferFileNames(const std::vector<px::FileEntry>& files) {
    // fs.rs:493 - 单文件传输允许空相对名(目标路径由传输元数据携带)
    if (files.size() == 1 && files.front().name().empty()) return;
    for (const auto& file : files) {
        if (file.name().empty()) Bail("empty file name in multi-file transfer");
        ValidateFileNameNoTraversal(file.name());
    }
}

void ValidateFsPathArgument(const std::string& path, const char* arg_name) {
    if (path.empty()) Bail(std::string(arg_name) + " cannot be empty");
    if (path.find('\0') != std::string::npos) {
        Bail(std::string(arg_name) + " contains null bytes");
    }
}

void ValidateNoSymlinkComponents(const std::filesystem::path& base, const std::string& name) {
    if (name.empty()) return;
    std::filesystem::path current = base;
    // Best-effort:基于路径的检查存在固有 TOCTOU 窗口(校验与写入之间本地文件系统可能变化),
    // 与 fs.rs:526 的注释一致;真正的防护需要 openat(2)/O_NOFOLLOW 级别的句柄打开。
    for (const auto& component : ToFsPath(name)) {
        // 等价于 std::path::Component 匹配:Normal 继续;CurDir(".") 跳过;其余拒绝
        if (component == ".") continue;
        if (component == ".." || component.has_root_name() || component.has_root_directory() ||
            ToUtf8(component).empty()) {
            Bail("invalid file name component");
        }
        current /= component;
        std::error_code ec;
        auto meta = std::filesystem::symlink_status(current, ec);
        if (!ec) {
            if (meta.type() == std::filesystem::file_type::symlink) {
                Bail("symlink path component is not allowed");
            }
        } else if (ec != std::errc::no_such_file_or_directory) {
            // 组件不存在 -> 继续 best-effort 校验;其他错误拒绝
            Bail("failed to validate path component '" + ToUtf8(current) + "': " + ec.message());
        }
    }
}

std::filesystem::path JoinValidatedPath(const std::filesystem::path& base, const std::string& name) {
    ValidateFileNameNoTraversal(name);
    ValidateNoSymlinkComponents(base, name);
    if (name.empty()) return base;
    return base / ToFsPath(name);
}

} // namespace px::ft
