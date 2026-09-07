//
// Created by RGAA on 29/05/2025.
//

#include "px_database.h"
#include "visit_record.h"
#include "visit_record_operator.h"
#include "file_transfer_record_operator.h"
#include "stream_db_operator.h"
#include "db_game_operator.h"
#include "sqlite_storage_config.h"
#include "px_common/folder_util.h"
#include "px_common/string_util.h"
#include "px_common/log.h"
#include <QDateTime>
#include <QDir>
#include <QApplication>

namespace px
{
    namespace {
        std::optional<std::string> BackupBrokenDatabaseFile(const std::string& db_path) {
            std::error_code ec;
            std::filesystem::path source = PathFromUTF8(db_path);
            if (!std::filesystem::exists(source, ec)) {
                return std::string{};
            }

            const auto ts = QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz").toStdString();
            auto backup = source;
            backup += ".corrupt." + ts + ".bak";
            std::filesystem::rename(source, backup, ec);
            if (ec) {
                LOGE("Backup broken database failed, path: {}, error: {}", db_path, ec.message());
                return std::nullopt;
            }

            return QString::fromStdWString(backup.wstring()).toStdString();
        }
    }

    PxDatabase::PxDatabase(const std::shared_ptr<PxContext>& ctx) {
        context_ = ctx;
    }

    bool PxDatabase::Init() {
        ready_ = false;
        last_error_.clear();
        db_storage_.reset();
        stream_operator_ = std::make_shared<StreamDBOperator>(shared_from_this());
        db_game_operator_ = std::make_shared<DBGameOperator>(context_, shared_from_this());
        visit_record_op_ = std::make_shared<VisitRecordOperator>(context_, shared_from_this());
        ft_record_op_ = std::make_shared<FileTransferRecordOperator>(context_, shared_from_this());

        std::string base_path = QString::fromStdWString(FolderUtil::GetProgramDataPath()).toStdString();
        auto db_path = base_path + "/px_data/px_data.db";
        if (!QDir().mkpath(QString::fromStdString(base_path + "/px_data"))) {
            last_error_ = std::format("Create database folder failed: {}", base_path + "/px_data");
            LOGE("{}", last_error_);
            return false;
        }

        try {
            auto storage = InitAppDatabase(db_path);
            ConfigureSqliteStorage(storage);
            storage.sync_schema();
            db_storage_ = storage;
        } catch (const std::exception& e) {
            LOGE("Init database failed, path: {}, error: {}", db_path, e.what());

            const auto backup_path = BackupBrokenDatabaseFile(db_path);
            if (!backup_path.has_value()) {
                last_error_ = std::format("Init database failed and backup failed: {}", e.what());
                return false;
            }
            if (!backup_path->empty()) {
                LOGE("Database was backed up and will be rebuilt, backup: {}", *backup_path);
            }

            try {
                auto storage = InitAppDatabase(db_path);
                ConfigureSqliteStorage(storage);
                storage.sync_schema();
                db_storage_ = storage;
            } catch (const std::exception& rebuild_error) {
                last_error_ = std::format("Rebuild database failed: {}", rebuild_error.what());
                LOGE("{}", last_error_);
                return false;
            }
        }

        ready_ = true;
        return true;
    }

    std::shared_ptr<VisitRecordOperator> PxDatabase::GetVisitRecordOp() {
        return visit_record_op_;
    }

    std::shared_ptr<FileTransferRecordOperator> PxDatabase::GetFileTransferRecordOp() {
        return ft_record_op_;
    }

    std::shared_ptr<StreamDBOperator> PxDatabase::GetStreamDBOperator() {
        return stream_operator_;
    }

    std::shared_ptr<DBGameOperator> PxDatabase::GetDBGameOperator() {
        return db_game_operator_;
    }

    std::vector<std::shared_ptr<VisitRecord>> PxDatabase::ScanUnclosedVisitRecords(int64_t before_timestamp) {
        if (visit_record_op_) {
            return visit_record_op_->ScanUnclosedRecords(before_timestamp);
        }
        return {};
    }

    std::vector<std::shared_ptr<FileTransferRecord>> PxDatabase::ScanUnclosedFileTransferRecords(int64_t before_timestamp) {
        if (ft_record_op_) {
            return ft_record_op_->ScanUnclosedRecords(before_timestamp);
        }
        return {};
    }

    bool PxDatabase::EnqueueAuditOutbox(const std::string& event_key, const std::string& endpoint,
                                        const std::string& payload, int64_t now) {
        if (!ready_ || event_key.empty() || endpoint.empty() || payload.empty()) {
            return false;
        }
        using Storage = decltype(GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(GetDbStorage());
        try {
            auto rows = storage.get_all<AuditOutboxRecord>(
                where(c(&AuditOutboxRecord::event_key_) == event_key), limit(1));
            if (!rows.empty()) {
                auto row = std::move(rows.front());
                row.endpoint_ = endpoint;
                row.payload_ = payload;
                row.next_attempt_at_ = now;
                row.last_error_.clear();
                storage.update(row);
            } else {
                AuditOutboxRecord row;
                row.event_key_ = event_key;
                row.endpoint_ = endpoint;
                row.payload_ = payload;
                row.created_at_ = now;
                row.next_attempt_at_ = now;
                storage.insert(row);
            }
            return true;
        } catch (const std::exception& e) {
            LOGE("EnqueueAuditOutbox failed, key: {}, error: {}", event_key, e.what());
            return false;
        }
    }

    std::optional<AuditOutboxRecord> PxDatabase::GetDueAuditOutbox(int64_t now) {
        if (!ready_) {
            return std::nullopt;
        }
        using Storage = decltype(GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(GetDbStorage());
        try {
            auto rows = storage.get_all<AuditOutboxRecord>(
                where(c(&AuditOutboxRecord::next_attempt_at_) <= now),
                order_by(&AuditOutboxRecord::next_attempt_at_), limit(1));
            if (rows.empty()) {
                return std::nullopt;
            }
            return std::move(rows.front());
        } catch (const std::exception& e) {
            LOGE("GetDueAuditOutbox failed: {}", e.what());
            return std::nullopt;
        }
    }

    void PxDatabase::CompleteAuditOutbox(int64_t id) {
        if (!ready_ || id <= 0) {
            return;
        }
        using Storage = decltype(GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(GetDbStorage());
        try {
            storage.remove<AuditOutboxRecord>(id);
        } catch (const std::exception& e) {
            LOGE("CompleteAuditOutbox failed, id: {}, error: {}", id, e.what());
        }
    }

    void PxDatabase::RetryAuditOutbox(int64_t id, int attempts, int64_t next_attempt_at,
                                      const std::string& last_error) {
        if (!ready_ || id <= 0) {
            return;
        }
        using Storage = decltype(GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(GetDbStorage());
        try {
            storage.update_all(
                set(c(&AuditOutboxRecord::attempts_) = attempts,
                    c(&AuditOutboxRecord::next_attempt_at_) = next_attempt_at,
                    c(&AuditOutboxRecord::last_error_) = last_error),
                where(c(&AuditOutboxRecord::id_) == id));
        } catch (const std::exception& e) {
            LOGE("RetryAuditOutbox failed, id: {}, error: {}", id, e.what());
        }
    }

    int PxDatabase::GetAuditOutboxCount() {
        if (!ready_) {
            return 0;
        }
        using Storage = decltype(GetStorageTypeValue());
        auto storage = std::any_cast<Storage>(GetDbStorage());
        return static_cast<int>(storage.count<AuditOutboxRecord>());
    }
}
