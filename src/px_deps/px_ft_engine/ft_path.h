// px_ft_engine - 路径与目录操作模块
// 对照 rustdesk/libs/hbb_common/src/fs.rs 中的路径/目录/安全校验函数逐条移植:
//   read_dir                -> ReadDir            (fs.rs:35, Windows 下 "/" 列盘符)
//   get_recursive_files     -> GetRecursiveFiles  (fs.rs:202)
//   get_empty_dirs_recursive-> GetEmptyDirsRecursive (fs.rs:244)
//   validate_file_name_no_traversal -> ValidateFileNameNoTraversal (fs.rs:460)
//   validate_no_symlink_components  -> ValidateNoSymlinkComponents (fs.rs:516)
//   join_validated_path             -> JoinValidatedPath (fs.rs:557)
//   remove_all_empty_dir / remove_file / create_dir / rename_file / transform_windows_path
//   is_compressed_file (fs.rs:454) 放在 ft_compress 模块。
//
// 约定:
// - 所有字符串路径为 UTF-8;Windows 下内部转成宽字符 std::filesystem::path 使用。
// - 失败统一抛 std::runtime_error(对应 fs.rs 的 bail!/anyhow),调用边界负责捕获。
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "px_file_transfer.pb.h"

namespace px::ft {

// UTF-8 字符串 <-> std::filesystem::path 互转(Windows 宽字符安全)
std::filesystem::path ToFsPath(const std::string& utf8);
std::string ToUtf8(const std::filesystem::path& p);

// 文件 mtime(Unix 秒)读写;失败返回 0 / false
uint64_t GetFileMtimeSecs(const std::filesystem::path& p);
bool SetFileMtimeSecs(const std::filesystem::path& p, uint64_t unix_secs);

// 用户主目录(对应 rustdesk Config::get_home)
std::string GetHomeAsString();

// ---------------- 目录操作 ----------------

// fs.rs:35 read_dir。Windows 下 path=="/" 时列盘符(A: B: ...,entry_type=DirDrive)。
px::FileDirectory ReadDir(const std::string& path, bool include_hidden);

// fs.rs:202 get_recursive_files:递归展开为相对路径文件列表。
// 单文件场景返回 1 个 name 为空的 FileEntry(沿用 fs.rs 语义)。
std::vector<px::FileEntry> GetRecursiveFiles(const std::string& path, bool include_hidden);

// fs.rs:244 get_empty_dirs_recursive
std::vector<px::FileDirectory> GetEmptyDirsRecursive(const std::string& path, bool include_hidden);

bool IsFileExists(const std::string& file_path);

// fs.rs:1409 create_dir(递归创建)
void CreateDir(const std::string& dir);

// fs.rs:1402 remove_file(单个文件)
void RemoveFile(const std::string& file);

// fs.rs:1384 remove_all_empty_dir:递归删除空目录;顺路删除符号链接项
void RemoveAllEmptyDir(const std::filesystem::path& path);

// fs.rs:1416 rename_file:new_name 只允许纯文件名(过 ValidateFileNameNoTraversal)
void RenameFile(const std::string& path, const std::string& new_name);

// fs.rs:1436 transform_windows_path:条目名 '\\' -> '/'
void TransformWindowsPath(std::vector<px::FileEntry>& entries);

// ---------------- 路径安全校验 ----------------

// fs.rs:460 validate_file_name_no_traversal
// 拒绝:空字节、".." 段(按 '/' 与 '\\' 切分)、Windows 盘符绝对路径(X:...)、
// 以 '/' 或 '\\' 开头的绝对/UNC 路径。
void ValidateFileNameNoTraversal(const std::string& name);

// fs.rs:490 validate_transfer_file_names(文件列表整体校验;单文件允许空名)
void ValidateTransferFileNames(const std::vector<px::FileEntry>& files);

// fs.rs:506 validate_fs_path_argument(非空 + 无空字节)
void ValidateFsPathArgument(const std::string& path, const char* arg_name);

// fs.rs:516 validate_no_symlink_components:逐组件检查 base/name 中已存在组件不是符号链接
// (best-effort,存在 TOCTOU 窗口,与上游一致并在注释中说明)
void ValidateNoSymlinkComponents(const std::filesystem::path& base, const std::string& name);

// fs.rs:557 join_validated_path:校验后拼接
std::filesystem::path JoinValidatedPath(const std::filesystem::path& base, const std::string& name);

} // namespace px::ft
