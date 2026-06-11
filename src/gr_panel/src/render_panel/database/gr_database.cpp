//
// Created by RGAA on 29/05/2025.
//

#include "gr_database.h"
#include "visit_record.h"
#include "visit_record_operator.h"
#include "file_transfer_record_operator.h"
#include "stream_db_operator.h"
#include "db_game_operator.h"
#include "tc_common_new/folder_util.h"
#include "tc_common_new/string_util.h"
#include "tc_common_new/log.h"
#include <QDateTime>
#include <QDir>
#include <QApplication>

namespace tc
{
    namespace {
        bool BackupBrokenDatabaseFile(const std::string& db_path, std::string* backup_path_out) {
            std::error_code ec;
            std::filesystem::path source = PathFromUTF8(db_path);
            if (!std::filesystem::exists(source, ec)) {
                return true;
            }

            const auto ts = QDateTime::currentDateTimeUtc().toString("yyyyMMddHHmmsszzz").toStdString();
            auto backup = source;
            backup += ".corrupt." + ts + ".bak";
            std::filesystem::rename(source, backup, ec);
            if (ec) {
                LOGE("Backup broken database failed, path: {}, error: {}", db_path, ec.message());
                return false;
            }

            if (backup_path_out) {
                const auto u8 = backup.u8string();
                *backup_path_out = std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
            }
            return true;
        }
    }

    GrDatabase::GrDatabase(const std::shared_ptr<GrContext>& ctx) {
        context_ = ctx;
    }

    bool GrDatabase::Init() {
        ready_ = false;
        last_error_.clear();
        db_storage_.reset();
        stream_operator_ = std::make_shared<StreamDBOperator>(shared_from_this());
        db_game_operator_ = std::make_shared<DBGameOperator>(context_, shared_from_this());
        visit_record_op_ = std::make_shared<VisitRecordOperator>(context_, shared_from_this());
        ft_record_op_ = std::make_shared<FileTransferRecordOperator>(context_, shared_from_this());

        std::string base_path = QString::fromStdWString(FolderUtil::GetProgramDataPath()).toStdString();
        auto db_path = base_path + "/gr_data/gr_data.db";
        if (!QDir().mkpath(QString::fromStdString(base_path + "/gr_data"))) {
            last_error_ = std::format("Create database folder failed: {}", base_path + "/gr_data");
            LOGE("{}", last_error_);
            return false;
        }

        try {
            auto storage = InitAppDatabase(db_path);
            storage.sync_schema();
            db_storage_ = storage;
        } catch (const std::exception& e) {
            LOGE("Init database failed, path: {}, error: {}", db_path, e.what());

            std::string backup_path;
            if (!BackupBrokenDatabaseFile(db_path, &backup_path)) {
                last_error_ = std::format("Init database failed and backup failed: {}", e.what());
                return false;
            }
            if (!backup_path.empty()) {
                LOGE("Database was backed up and will be rebuilt, backup: {}", backup_path);
            }

            try {
                auto storage = InitAppDatabase(db_path);
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

    std::shared_ptr<VisitRecordOperator> GrDatabase::GetVisitRecordOp() {
        return visit_record_op_;
    }

    std::shared_ptr<FileTransferRecordOperator> GrDatabase::GetFileTransferRecordOp() {
        return ft_record_op_;
    }

    std::shared_ptr<StreamDBOperator> GrDatabase::GetStreamDBOperator() {
        return stream_operator_;
    }

    std::shared_ptr<DBGameOperator> GrDatabase::GetDBGameOperator() {
        return db_game_operator_;
    }
}
