//
// Created by RGAA on 29/05/2025.
//

#include "file_transfer_record.h"
#include "px_common/time_util.h"
#include <nlohmann/json.hpp>
#include <format>

using namespace nlohmann;

namespace px
{

    const std::string FileTransferRecord::kUrlInsertFileTransferRecord = "/api/v1/record/upload_file_transfer_info";

    const std::string FileTransferRecord::kUrlUpdateFileTransferRecord = "/api/v1/record/update_file_transfer_info";

    std::string FileTransferRecord::AsString() {
        return std::format("Begin: {}, End: {}, Controller device: {}, Controlled device: {}, direction: {}, file detail: {}",
                           TimeUtil::FormatTimestamp(begin_), TimeUtil::FormatTimestamp(end_), visitor_device_,target_device_, direction_, file_detail_);
    }

    std::string FileTransferRecord::AsJson() {
        nlohmann::json obj;
        obj["the_file_id"] = the_file_id_;
        obj["begin"] = TimeUtil::FormatTimestamp(begin_);
        obj["end"] = TimeUtil::FormatTimestamp(end_);
        obj["visitor_device"] = visitor_device_;
        obj["target_device"] = target_device_;
        obj["direction"] = direction_;
        obj["file_detail"] = file_detail_;
        obj["success"] = success_;
        obj["duration"] = duration_;
        obj["status"] = status_;
        obj["end_reason"] = end_reason_;
        obj["recovered"] = recovered_;
        return obj.dump(2);
    }

    std::string FileTransferRecord::AsJson2() {
        nlohmann::json obj;
        obj["the_file_id"] = the_file_id_;
        obj["begin"] = begin_;
        // Always serialize a lifecycle-begin event.  Recovery may call this
        // after the local row has already been finalized.
        obj["end"] = 0;
        obj["visitor_device"] = visitor_device_;
        obj["target_device"] = target_device_;
        obj["direction"] = direction_;
        obj["file_detail"] = file_detail_;
        obj["duration"] = 0;
        obj["success"] = false;
        obj["status"] = "running";
        obj["end_reason"] = "";
        obj["recovered"] = false;
        return obj.dump(2);
    }

    std::string FileTransferRecord::AsUpdateJson() {
        nlohmann::json obj;
        obj["the_file_id"] = the_file_id_;
        obj["end"] = end_;
        obj["success"] = success_;
        obj["duration"] = duration_;
        obj["status"] = status_;
        obj["end_reason"] = end_reason_;
        obj["recovered"] = recovered_;
        return obj.dump(2);
    }
}
