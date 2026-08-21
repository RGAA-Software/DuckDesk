//
// Created by RGAA on 29/05/2025.
//

#ifndef PX_VISIT_RECORD_H
#define PX_VISIT_RECORD_H

#include <string>

namespace px
{

    class VisitRecord {
    public:
        [[nodiscard]] bool IsHeaderItem() const {
            return id_ == 0 && visitor_device_.empty() && target_device_.empty();
        }

        std::string AsString();
        std::string AsJson();
        std::string AsJson2();
        std::string AsUpdateJson();

    public:
        int id_{};
        std::string conn_id_;
        std::string stream_id_;
        std::string conn_type_;
        // unit: ms
        int64_t begin_{0};
        // unit: ms
        int64_t end_{0};
        // unit: ms
        int64_t duration_{0};
        std::string visitor_device_;
        std::string target_device_;
        // running / succeeded / aborted
        std::string status_{"running"};
        std::string end_reason_;
        bool recovered_{false};
    };

}

#endif //PX_VISIT_RECORD_H
