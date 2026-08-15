//
// Cross-platform clipboard content types.
//

#ifndef TC_COMMON_NEW_CLIPBOARD_TYPES_H
#define TC_COMMON_NEW_CLIPBOARD_TYPES_H

#include <cstdint>
#include <string>
#include <vector>

namespace tc::clipboard
{

    struct FileEntry {
        std::string full_path_;
        std::string file_name_;
        std::string ref_path_;
        int64_t total_size_ = 0;
    };

    struct Content {
        std::string text_;
        std::vector<FileEntry> files_;

        bool HasText() const { return !text_.empty(); }
        bool HasFiles() const { return !files_.empty(); }
        bool Empty() const { return !HasText() && !HasFiles(); }
    };

}

#endif
