//
// Created by RGAA on 29/05/2025.
//

#ifndef PX_FILE_TRANSFER_RECORD_OPERATOR_H
#define PX_FILE_TRANSFER_RECORD_OPERATOR_H

#include <memory>
#include <string>
#include <vector>
#include <optional>
#include <mutex>

namespace px
{

    class PxContext;
    class PxDatabase;
    class FileTransferRecord;

    class FileTransferRecordOperator {
    public:
        FileTransferRecordOperator(const std::shared_ptr<PxContext>& ctx, const std::shared_ptr<PxDatabase>& db);

        void InsertFileTransferRecord(const std::shared_ptr<FileTransferRecord>& record);
        void UpdateFileTransferRecord(const std::string& the_file_id, int64_t end_timestamp, bool success);
        std::optional<std::shared_ptr<FileTransferRecord>> GetFileTransferRecordByFileId(const std::string& the_file_id);
        std::vector<std::shared_ptr<FileTransferRecord>> QueryFileTransferRecords(int page, int page_size);
        std::vector<std::shared_ptr<FileTransferRecord>> ScanUnclosedRecords(int64_t before_timestamp);
        void Delete(int id);
        void DeleteAll();
        int GetTotalCounts();
        void FlushPendingRecords();

    private:
        std::shared_ptr<PxDatabase> db_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::mutex pending_mutex_;
        std::vector<std::shared_ptr<FileTransferRecord>> pending_records_;
    };

}

#endif //PX_VISIT_RECORD_OPERATOR_H
