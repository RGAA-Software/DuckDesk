#include "clipboard_file_builder.h"

#include "px_common/folder_util.h"

#include "px_common/string_util.h"

#include "px_common/log.h"

#include <climits>

#include <filesystem>

namespace fs = std::filesystem;

namespace px::clipboard
{
    namespace {

        std::string NormalizeRefPath(std::string ref_path) {
            for (auto& ch : ref_path) {
                if (ch == '\\') {
                    ch = '/';
                }
            }
            return ref_path;
        }

        std::optional<FileEntry> MakeFileEntry(const fs::path& full_path, const std::string& ref_path) {
            std::error_code ec;
            if (!fs::exists(full_path, ec) || ec) {
                return std::nullopt;
            }

            FileEntry entry;
            entry.full_path_ = PathToUTF8(full_path);
            entry.file_name_ = PathToUTF8(full_path.filename());
            entry.ref_path_ = NormalizeRefPath(ref_path);
            entry.total_size_ = static_cast<int64_t>(fs::file_size(full_path, ec));
            if (ec) {
                entry.total_size_ = 0;
            }
            return entry;
        }

        std::optional<std::string> RelativeRefPath(const fs::path& base_folder, const fs::path& full_path) {
            const auto base_u8 = PathToUTF8(base_folder);
            const auto full_u8 = PathToUTF8(full_path);
            if (full_u8.find(base_u8) != 0) {
                LOGE("clipboard file not under base folder, {} => {}", base_u8, full_u8);
                return std::nullopt;
            }

            std::string ref_path = full_u8.substr(base_u8.size());
            while (!ref_path.empty() && (ref_path.front() == '/' || ref_path.front() == '\\')) {
                ref_path.erase(ref_path.begin());
            }
            return NormalizeRefPath(ref_path);
        }

        void CollectFilesRecursive(const fs::path& path, std::vector<std::string>& out) {
            std::error_code ec;
            if (fs::is_directory(path, ec)) {
                FolderUtil::VisitRecursiveFiles(path, 0, INT_MAX, [&](VisitResult&& r) {
                    out.push_back(PathToUTF8(r.path_));
                });
            } else if (fs::exists(path, ec)) {
                out.push_back(PathToUTF8(path));
            }
        }

        void AppendDirectoryEntries(const fs::path& dir, std::vector<FileEntry>& entries) {
            const auto dir_name = PathToUTF8(dir.filename());
            if (dir_name.empty()) {
                return;
            }

            std::vector<std::string> expanded;
            CollectFilesRecursive(dir, expanded);

            std::error_code ec;
            const auto base = fs::absolute(dir, ec);

            for (const auto& full_u8 : expanded) {
                const auto full_path = PathFromUTF8(full_u8);
                auto rel = RelativeRefPath(base, full_path);
                if (!rel.has_value()) {
                    continue;
                }
                std::string ref_path = dir_name;
                if (!rel->empty()) {
                    ref_path += "/";
                    ref_path += rel.value();
                }
                if (auto entry = MakeFileEntry(full_path, ref_path)) {
                    entries.push_back(entry.value());
                }
            }
        }

        std::vector<FileEntry> BuildFileOnlyEntries(const std::vector<std::string>& full_paths) {
            std::vector<std::string> expanded;
            for (const auto& p : full_paths) {
                const auto path = PathFromUTF8(p);
                std::error_code ec;
                if (fs::is_regular_file(path, ec)) {
                    expanded.push_back(p);
                }
            }
            if (expanded.empty()) {
                return {};
            }

            fs::path base_folder = PathFromUTF8(expanded[0]).parent_path();
            std::vector<FileEntry> entries;
            for (const auto& full_u8 : expanded) {
                const auto full_path = PathFromUTF8(full_u8);
                auto rel = RelativeRefPath(base_folder, full_path);
                if (!rel.has_value()) {
                    continue;
                }
                if (auto entry = MakeFileEntry(full_path, rel.value())) {
                    entries.push_back(entry.value());
                }
            }
            return entries;
        }

        bool HasDirectoryRoot(const std::vector<std::string>& full_paths) {
            for (const auto& p : full_paths) {
                std::error_code ec;
                if (fs::is_directory(PathFromUTF8(p), ec)) {
                    return true;
                }
            }
            return false;
        }

    }

    std::optional<std::vector<FileEntry>> BuildFileEntriesFromPaths(const std::vector<std::string>& full_paths) {
        if (full_paths.empty()) {
            return std::nullopt;
        }

        std::vector<FileEntry> entries;
        if (HasDirectoryRoot(full_paths)) {
            for (const auto& p : full_paths) {
                const auto path = PathFromUTF8(p);
                std::error_code ec;
                if (fs::is_directory(path, ec)) {
                    AppendDirectoryEntries(path, entries);
                } else if (fs::is_regular_file(path, ec)) {
                    if (auto entry = MakeFileEntry(path, PathToUTF8(path.filename()))) {
                        entries.push_back(entry.value());
                    }
                }
            }
        } else {
            entries = BuildFileOnlyEntries(full_paths);
        }

        if (entries.empty()) {
            return std::nullopt;
        }
        return entries;
    }

}
