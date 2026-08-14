#ifndef TC_COMMON_NEW_CLIPBOARD_FILE_BUILDER_H
#define TC_COMMON_NEW_CLIPBOARD_FILE_BUILDER_H

#include "clipboard_types.h"
#include <optional>
#include <vector>

namespace tc::clipboard
{

    // Build sync metadata from absolute file paths (common parent folder + ref_path).
    std::optional<std::vector<FileEntry>> BuildFileEntriesFromPaths(const std::vector<std::string>& full_paths);

}

#endif
