//
// Created by RGAA on 2024/3/29.
//

#ifndef TC_SERVER_STEAM_FOLDER_UTIL_H
#define TC_SERVER_STEAM_FOLDER_UTIL_H

#include <string>
#include <functional>
#include <filesystem>
#include "string_util.h"

namespace fs = std::filesystem;

namespace tc
{

    class VisitResult {
    public:
        std::wstring name_;
        std::wstring path_;
    };

    class FolderUtil {
    public:

        static void VisitFiles(const U8Path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static void VisitFolders(const U8Path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static void VisitAll(const U8Path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static void VisitRecursiveFiles(const U8Path& path, int depth, int max_depth, const std::function<void(VisitResult&&)>&, const std::string& filter_suffix = "");
        // source:
        // destination:
        // ignore_suffix: lowercase, eg: {".h264", ".h265"}
        // overwrite:
        static bool CopyDir(const U8Path& source,
                            const U8Path& destination,
                            const std::vector<std::string>& ignore_suffix = {},
                            bool overwrite = true);
        //
        static bool CopyDir(const U8Path& source,
                            const U8Path& destination,
                            std::function<bool(const std::string& path, const std::string& filename)>&& ignore_predicate,
                            bool overwrite = true);

        static std::wstring GetProgramDataPath(const std::string& app = "GoDesk");

        static bool DeleteDir(const U8Path& path);

#ifdef WIN32
        static void VisitAllByQt(const U8Path& path, std::function<void(VisitResult&&)>&&, const std::string& filter_suffix = "");
        static std::wstring GetCurrentFilePath();
        static std::wstring GetCurrentFolderPath();
        static void CreateDir(const U8Path& path);
        static void OpenDir(const U8Path& path);
#endif
    };
}

#endif //TC_SERVER_STEAM_FOLDER_UTIL_H
