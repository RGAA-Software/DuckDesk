//
// Created by RGAA on 29/05/2025.
//

#include "file_transfer_record_operator.h"
#include "px_database.h"
#include "file_transfer_record.h"
#include "px_common_new/log.h"

namespace px
{
    namespace {
        bool IsDbReady(const std::shared_ptr<GrDatabase>& db) {
            if (!db || !db->IsReady()) {
                LOGE("FileTransferRecordOperator ignored because database is not ready");
                return false;
            }
            return true;
        }
    }

    FileTransferRecordOperator::FileTransferRecordOperator(const std::shared_ptr<GrContext>& ctx, const std::shared_ptr<GrDatabase>& db) {
        context_ = ctx;
        db_ = db;
    }

    void FileTransferRecordOperator::InsertFileTransferRecord(const std::shared_ptr<FileTransferRecord>& record) {
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
            LOGE("InsertFileTransferRecord failed: {}", e.what());
        }
    }

    void FileTransferRecordOperator::UpdateFileTransferRecord(const std::string& the_file_id, int64_t end_timestamp, bool success) {
        if (!IsDbReady(db_)) {
            return;
        }
        auto opt_record = GetFileTransferRecordByFileId(the_file_id);
        if (!opt_record.has_value()) {
            return;
        }

        const auto& record = opt_record.value();
        record->end_ = end_timestamp;
        record->success_ = success;
        record->duration_ = (record->begin_ > 0 && end_timestamp > record->begin_)
            ? end_timestamp - record->begin_
            : 0;
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto streams = storage.get_all<FileTransferRecord>(where(c(&FileTransferRecord::the_file_id_) == the_file_id));
        if (!streams.empty()) {
            storage.update(*record);
        }
    }

    std::optional<std::shared_ptr<FileTransferRecord>> FileTransferRecordOperator::GetFileTransferRecordByFileId(const std::string& the_file_id) {
        if (!IsDbReady(db_)) {
            return std::nullopt;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto records = storage.get_all_pointer<FileTransferRecord>(where(c(&FileTransferRecord::the_file_id_) == the_file_id));
        if (records.empty()) {
            return std::nullopt;
        }
        auto record = std::move(records[0]);
        return record;
    }

    std::vector<std::shared_ptr<FileTransferRecord>> FileTransferRecordOperator::QueryFileTransferRecords(int page, int page_size) {
        if (!IsDbReady(db_)) {
            return {};
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto qrs = storage.get_all_pointer<FileTransferRecord>(
                                                    order_by(&FileTransferRecord::id_),
                                                    limit(page_size, offset((page-1)*page_size)));
        std::vector<std::shared_ptr<FileTransferRecord>> records;
        for (auto& r : qrs) {
            records.push_back(std::move(r));
        }
        return records;
    }

    std::vector<std::shared_ptr<FileTransferRecord>> FileTransferRecordOperator::ScanUnclosedRecords(int64_t before_timestamp) {
        if (!IsDbReady(db_)) {
            return {};
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto qrs = storage.get_all_pointer<FileTransferRecord>(
            where(c(&FileTransferRecord::end_) == 0 && c(&FileTransferRecord::begin_) < before_timestamp));
        std::vector<std::shared_ptr<FileTransferRecord>> records;
        for (auto& r : qrs) {
            records.push_back(std::move(r));
        }
        return records;
    }

    void FileTransferRecordOperator::Delete(int id) {
        if (!IsDbReady(db_)) {
            return;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        storage.remove<FileTransferRecord>(id);
    }

    void FileTransferRecordOperator::DeleteAll() {
        if (!IsDbReady(db_)) {
            return;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        storage.remove_all<FileTransferRecord>();
    }

    int FileTransferRecordOperator::GetTotalCounts() {
        if (!IsDbReady(db_)) {
            return 0;
        }
        using Storage = decltype(db_->GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(db_->GetDbStorage());
        auto count = storage.count<FileTransferRecord>();
        return count;
    }

    void FileTransferRecordOperator::FlushPendingRecords() {
        std::vector<std::shared_ptr<FileTransferRecord>> records;
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            records = std::move(pending_records_);
        }
        for (const auto& record : records) {
            InsertFileTransferRecord(record);
        }
    }

}
