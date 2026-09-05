//
// Created by RGAA on 2024/3/29.
//

#ifndef TC_SERVER_STEAM_FOLDER_UTIL_H
#define TC_SERVER_STEAM_FOLDER_UTIL_H

#include <string>
#include <functional>
#include <filesystem>

namespace fs = std::filesystem;

namespace px
{

    class VisitResult {
    public:
        std::wstring name_;
        std::wstring path_;
    };

    class FolderUtil {
    public:

        static void VisitFiles(const std::filesystem::path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static void VisitFolders(const std::filesystem::path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static void VisitAll(const std::filesystem::path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static void VisitRecursiveFiles(const std::filesystem::path& path, int depth, int max_depth,
                                        const std::function<void(VisitResult&&)>&, const std::string& filter_suffix = "");
        // source:
        // destination:
        // ignore_suffix: lowercase, eg: {".h264", ".h265"}
        // overwrite:
        static bool CopyDir(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            const std::vector<std::string>& ignore_suffix = {},
                            bool overwrite = true);
        //
        static bool CopyDir(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            std::function<bool(const std::string& path, const std::string& filename)>&& ignore_predicate,
                            bool overwrite = true);

        static std::wstring GetProgramDataPath(const std::string& app = "Pixels");

        static bool DeleteDir(const std::filesystem::path& path);

#ifdef WIN32
        static void VisitAllByQt(const std::filesystem::path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static std::wstring GetCurrentFilePath();
        static std::wstring GetCurrentFolderPath();
        static void CreateDir(const std::filesystem::path& path);
        static void OpenDir(const std::filesystem::path& path);
#endif
    };
}

#endif //TC_SERVER_STEAM_FOLDER_UTIL_H
