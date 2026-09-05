//
// Created by RGAA on 29/05/2025.
//

#include "visit_record.h"
#include <format>
#include "px_common/time_util.h"
#include <nlohmann/json.hpp>

namespace px
{

    std::string VisitRecord::AsString() {
        return std::format("Conn Type: {}, Begin: {}, End: {}, Duration: {}, Visitor device: {}, Target device: {}",
                           connection_type_,
                           TimeUtil::FormatTimestamp(begin_),
                           TimeUtil::FormatTimestamp(end_),
                           TimeUtil::FormatSecondsToDHMS(duration_),
                           visitor_device_,
                           target_device_);
    }

    std::string VisitRecord::AsJson() {
        nlohmann::json obj;
        obj["conn_id"] = connection_id_;
        obj["stream_id"] = stream_id_;
        obj["conn_type"] = connection_type_;
        obj["begin"] = TimeUtil::FormatTimestamp(begin_);
        obj["end"] = TimeUtil::FormatTimestamp(end_);
        obj["duration"] = TimeUtil::FormatSecondsToDHMS(duration_);
        obj["visitor_device"] = visitor_device_;
        obj["target_device"] = target_device_;
        obj["status"] = status_;
        obj["end_reason"] = end_reason_;
        obj["recovered"] = recovered_;
        return obj.dump();
    }

    std::string VisitRecord::AsJson2() {
        nlohmann::json obj;
        obj["conn_id"] = connection_id_;
        obj["stream_id"] = stream_id_;
        obj["conn_type"] = connection_type_;
        obj["begin"] = begin_;
        // The create endpoint represents the beginning of a lifecycle.  Keep
        // this payload in the running state even when it is reconstructed
        // from a locally finalized record during recovery.
        obj["end"] = 0;
        obj["duration"] = 0;
        obj["visitor_device"] = visitor_device_;
        obj["target_device"] = target_device_;
        obj["status"] = "running";
        obj["end_reason"] = "";
        obj["recovered"] = false;
        return obj.dump();
    }

    std::string VisitRecord::AsUpdateJson() {
        nlohmann::json obj;
        obj["conn_id"] = connection_id_;
        obj["end"] = end_;
        obj["duration"] = duration_;
        obj["status"] = status_;
        obj["end_reason"] = end_reason_;
        obj["recovered"] = recovered_;
        return obj.dump();
    }
}
