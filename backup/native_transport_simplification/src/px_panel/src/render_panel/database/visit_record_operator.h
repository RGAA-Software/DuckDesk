//
// Created by RGAA on 29/05/2025.
//

#ifndef PX_VISIT_RECORD_OPERATOR_H
#define PX_VISIT_RECORD_OPERATOR_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace px
{

    class PxContext;
    class PxDatabase;
    class VisitRecord;

    class VisitRecordOperator {
    public:
        VisitRecordOperator(const std::shared_ptr<PxContext>& ctx, const std::shared_ptr<PxDatabase>& db);

        void InsertVisitRecord(const std::shared_ptr<VisitRecord>& record);
        void UpdateVisitRecord(const std::string& conn_id, int64_t end_timestamp, int64_t duration,
                               const std::string& status = "succeeded",
                               const std::string& end_reason = "client_disconnected",
                               bool recovered = false);
        std::optional<std::shared_ptr<VisitRecord>> GetVisitRecordConnId(const std::string& conn_id);
        std::vector<std::shared_ptr<VisitRecord>> QueryVisitRecords(int page, int page_size);
        std::vector<std::shared_ptr<VisitRecord>> ScanUnclosedRecords(int64_t before_timestamp);
        void Delete(int id);
        void DeleteAll();
        int GetTotalCounts();
        void FlushPendingRecords();

    private:
        std::shared_ptr<PxDatabase> db_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::mutex pending_mutex_;
        std::vector<std::shared_ptr<VisitRecord>> pending_records_;
    };

}

#endif //PX_VISIT_RECORD_OPERATOR_H
