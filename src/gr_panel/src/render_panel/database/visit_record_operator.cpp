//
// Created by RGAA on 29/05/2025.
//

#include "visit_record_operator.h"
#include "gr_database.h"
#include "visit_record.h"
#include "tc_common_new/log.h"

namespace tc
{
    namespace {
        bool IsDbReady(const std::shared_ptr<GrDatabase>& db) {
            if (!db || !db->IsReady()) {
                LOGE("VisitRecordOperator ignored because database is not ready");
                return false;
            }
            return true;
        }
    }

    VisitRecordOperator::VisitRecordOperator(const std::shared_ptr<GrContext>& ctx, const std::shared_ptr<GrDatabase>& db) {
        context_ = ctx;
        db_ = db;
    }

    void VisitRecordOperator::InsertVisitRecord(const std::shared_ptr<VisitRecord>& record) {
        if (!record) {
            return;
        }
        if (!IsDbReady(db_)) {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_records_.push_back(record);
            return;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        try {
            storage.replace(*record);
        } catch (const std::exception& e) {
            LOGE("InsertVisitRecord failed: {}", e.what());
        }
    }

    void VisitRecordOperator::UpdateVisitRecord(const std::string& conn_id, int64_t end_timestamp, int64_t duration) {
        if (!IsDbReady(db_)) {
            return;
        }
        auto opt_record = GetVisitRecordConnId(conn_id);
        if (!opt_record.has_value()) {
            return;
        }

        const auto& record = opt_record.value();
        record->end_ = end_timestamp;
        record->duration_ = duration;
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto streams = storage.get_all<VisitRecord>(where(c(&VisitRecord::conn_id_) == conn_id));
        if (!streams.empty()) {
            storage.update(*record);
        }
    }

    std::optional<std::shared_ptr<VisitRecord>> VisitRecordOperator::GetVisitRecordConnId(const std::string& conn_id) {
        if (!IsDbReady(db_)) {
            return std::nullopt;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto records = storage.get_all_pointer<VisitRecord>(where(c(&VisitRecord::conn_id_) == conn_id));
        if (records.empty()) {
            return std::nullopt;
        }
        auto record = std::move(records[0]);
        return record;
    }

    std::vector<std::shared_ptr<VisitRecord>> VisitRecordOperator::QueryVisitRecords(int page, int page_size) {
        if (!IsDbReady(db_)) {
            return {};
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto qrs = storage.get_all_pointer<VisitRecord>(
                                                    order_by(&VisitRecord::id_),
                                                    limit(page_size, offset((page-1)*page_size)));
        std::vector<std::shared_ptr<VisitRecord>> records;
        for (auto& r : qrs) {
            records.push_back(std::move(r));
        }
        return records;
    }

    std::vector<std::shared_ptr<VisitRecord>> VisitRecordOperator::ScanUnclosedRecords(int64_t before_timestamp) {
        if (!IsDbReady(db_)) {
            return {};
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto qrs = storage.get_all_pointer<VisitRecord>(
            where(c(&VisitRecord::end_) == 0 && c(&VisitRecord::begin_) < before_timestamp));
        std::vector<std::shared_ptr<VisitRecord>> records;
        for (auto& r : qrs) {
            records.push_back(std::move(r));
        }
        return records;
    }

    void VisitRecordOperator::Delete(int id) {
        if (!IsDbReady(db_)) {
            return;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        storage.remove<VisitRecord>(id);
    }

    void VisitRecordOperator::DeleteAll() {
        if (!IsDbReady(db_)) {
            return;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        storage.remove_all<VisitRecord>();
    }

    int VisitRecordOperator::GetTotalCounts() {
        if (!IsDbReady(db_)) {
            return 0;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto count = storage.count<VisitRecord>();
        return count;
    }

    void VisitRecordOperator::FlushPendingRecords() {
        std::vector<std::shared_ptr<VisitRecord>> records;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            records = std::move(pending_records_);
        }
        for (const auto& record : records) {
            InsertVisitRecord(record);
        }
    }

}
