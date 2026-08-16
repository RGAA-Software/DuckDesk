//
// Created by RGAA on 29/05/2025.
//

#ifndef PX_FILE_TRANSFER_RECORD_H
#define PX_FILE_TRANSFER_RECORD_H

#include <string>

namespace px
{

    class FileTransferRecord {
    public:
        [[nodiscard]] bool IsHeaderItem() const {
            return id_ == 0 && visitor_device_.empty() && target_device_.empty();
        }

        std::string AsString();
        std::string AsJson();
        std::string AsJson2();
        std::string AsUpdateJson();

        static const std::string kUrlInsertFileTransferRecord;
        static const std::string kUrlUpdateFileTransferRecord;
    public:
        int id_{0};
        std::string the_file_id_;
        int64_t begin_{0};
        int64_t end_{0};
        std::string visitor_device_;
        std::string target_device_;
        std::string direction_;
        std::string file_detail_;
        bool success_ = false;
        int64_t duration_{0};
    };

}

#endif //PX_FILE_TRANSFER_RECORD_H
