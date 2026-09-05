//
// Created by RGAA on 2024-02-06.
//

#ifndef TC_APPLICATION_FILE_EXT_H
#define TC_APPLICATION_FILE_EXT_H

#include <string>
#include <filesystem>

namespace px
{

    class FileUtil {
    public:
        static std::string GetFileNameFromPath(const std::string& path);
        static std::string GetFileNameFromPathNoSuffix(const std::string& path);
        // only return suffix name, eg: h264
        static std::string GetFileSuffix(const std::string& path);
        static std::string GetFileFolder(const std::string& path);
        static std::string GetFolderNameFormAbsFolderPath(const std::string& path);
        static bool CopyFileExt(const std::filesystem::path& from, const std::filesystem::path& to, bool force_replace);
        static void SelectFileInExplorer(const std::filesystem::path& path);
        static bool ReName(const std::filesystem::path& old_path, const std::filesystem::path& new_path);
    };

}

#endif //TC_APPLICATION_FILE_EXT_H
