
#ifndef DOLINK_ZIP_UTIL_H
#define DOLINK_ZIP_UTIL_H

#include <string>

namespace tc
{

    class ZipUtil {
    public:
        static bool ZipFolder(const std::wstring& directory_path, const std::wstring& zip_file_path);
    };

}

#endif //DOLINK_ZIP_UTIL_H
