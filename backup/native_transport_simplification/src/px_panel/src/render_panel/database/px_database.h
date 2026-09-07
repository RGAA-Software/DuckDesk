//
// Created by RGAA on 29/05/2025.
//

#ifndef PX_DATABASE_H
#define PX_DATABASE_H

#include <any>
#include <memory>
#include <optional>
extern "C" {
#include <sqlite3.h>
}
#include <sqlite_orm/sqlite_orm.h>
#include "db_game.h"
#include "visit_record.h"
#include "file_transfer_record.h"
#include "px_console_client/console_stream.h"

using namespace sqlite_orm;

namespace px
{

    class AuditOutboxRecord {
    public:
        int64_t id_{0};
        std::string event_key_;
        std::string endpoint_;
        std::string payload_;
        int attempts_{0};
        int64_t created_at_{0};
        int64_t next_attempt_at_{0};
        std::string last_error_;
    };

    class PxContext;
    class StreamDBOperator;
    class DBGameOperator;
    class VisitRecordOperator;
    class FileTransferRecordOperator;

    class PxDatabase : public std::enable_shared_from_this<PxDatabase> {
    public:
        explicit PxDatabase(const std::shared_ptr<PxContext>& ctx);
        bool Init();
        bool IsReady() const { return ready_; }
        const std::string& GetLastError() const { return last_error_; }
        auto InitAppDatabase(const std::string& name) {
            auto st = make_storage(name,
                make_table("games",
                    make_column("id", &TcDBGame::id_, primary_key().autoincrement()),
                    make_column("game_id", &TcDBGame::game_id_),
                    make_column("game_name", &TcDBGame::game_name_),
                    make_column("game_installed_dir", &TcDBGame::game_installed_dir_),
                    make_column("game_exes", &TcDBGame::game_exes_),
                    make_column("game_exe_names", &TcDBGame::game_exe_names_),
                    make_column("is_installed", &TcDBGame::is_installed_),
                    make_column("steam_url", &TcDBGame::steam_url_),
                    make_column("cover_name", &TcDBGame::cover_name_),
                    make_column("engine_type", &TcDBGame::engine_type_),
                    make_column("cover_url", &TcDBGame::cover_url_)
                ),
                make_table("stream",
                    make_column("id", &px_console::ConsoleStream::_id, primary_key()),
                    make_column("stream_id", &px_console::ConsoleStream::stream_id_),
                    make_column("stream_name", &px_console::ConsoleStream::stream_name_),
                    make_column("encode_bps", &px_console::ConsoleStream::encode_bps_),
                    make_column("audio_enabled", &px_console::ConsoleStream::audio_enabled_, default_value(0)),
                    make_column("clipboard_enabled", &px_console::ConsoleStream::clipboard_enabled_, default_value(0)),
                    make_column("only_viewing", &px_console::ConsoleStream::only_viewing_, default_value(0)),
                    make_column("show_max_window", &px_console::ConsoleStream::auto_layout_screens_, default_value(0)),
                    make_column("split_windows", &px_console::ConsoleStream::split_windows_, default_value(0)),
                    make_column("audio_capture_mode", &px_console::ConsoleStream::audio_capture_mode_),
                    make_column("stream_host", &px_console::ConsoleStream::stream_host_),
                    make_column("stream_port", &px_console::ConsoleStream::stream_port_),
                    make_column("relay_host", &px_console::ConsoleStream::relay_host_, default_value("")),
                    make_column("relay_port", &px_console::ConsoleStream::relay_port_, default_value(0)),
                    //make_column("relay_appkey", &px_console::ConsoleStream::relay_appkey_, default_value("")),
                    make_column("bg_color", &px_console::ConsoleStream::bg_color_),
                    make_column("encode_fps", &px_console::ConsoleStream::encode_fps_),
                    make_column("connect_type", &px_console::ConsoleStream::connect_type_),
                    make_column("device_id", &px_console::ConsoleStream::device_id_),
                    make_column("device_random_pwd", &px_console::ConsoleStream::device_random_pwd_),
                    make_column("device_safety_pwd", &px_console::ConsoleStream::device_safety_pwd_),
                    make_column("remote_device_id", &px_console::ConsoleStream::remote_device_id_),
                    make_column("remote_device_random_pwd", &px_console::ConsoleStream::remote_device_random_pwd_),
                    make_column("remote_device_safety_pwd", &px_console::ConsoleStream::remote_device_safety_pwd_),
                    make_column("created_timestamp", &px_console::ConsoleStream::created_timestamp_, default_value(0)),
                    make_column("updated_timestamp", &px_console::ConsoleStream::updated_timestamp_, default_value(0)),
                    make_column("enable_p2p", &px_console::ConsoleStream::enable_p2p_, default_value(0)),
                    make_column("desktop_name", &px_console::ConsoleStream::desktop_name_),
                    make_column("os_version", &px_console::ConsoleStream::os_version_),
                    make_column("force_relay", &px_console::ConsoleStream::force_relay_, default_value(false)),
                    make_column("force_direct", &px_console::ConsoleStream::force_direct_, default_value(false)),
                    make_column("force_software", &px_console::ConsoleStream::force_software_, default_value(false)),
                    make_column("wait_debug", &px_console::ConsoleStream::wait_debug_, default_value(false)),
                    make_column("force_gdi_capture", &px_console::ConsoleStream::force_gdi_capture_, default_value(false)),
                    make_column("disable_vulkan_render", &px_console::ConsoleStream::disable_vulkan_render_, default_value(false)),
                    make_column("use_webrtc", &px_console::ConsoleStream::use_webrtc_, default_value(false)),
                    make_column("use_udp", &px_console::ConsoleStream::use_udp_, default_value(false))
                ),
                make_table("visit_record",
                    make_column("id", &VisitRecord::id_, primary_key()),
                    make_column("stream_id", &VisitRecord::stream_id_),
                    make_column("conn_id", &VisitRecord::connection_id_, unique()),
                    make_column("conn_type", &VisitRecord::connection_type_),
                    make_column("begin", &VisitRecord::begin_),
                    make_column("end", &VisitRecord::end_),
                    make_column("duration", &VisitRecord::duration_),
                    make_column("visitor_device", &VisitRecord::visitor_device_),
                    make_column("target_device", &VisitRecord::target_device_),
                    make_column("status", &VisitRecord::status_, default_value(std::string("running"))),
                    make_column("end_reason", &VisitRecord::end_reason_, default_value(std::string())),
                    make_column("recovered", &VisitRecord::recovered_, default_value(false))
                ),
                make_table("file_transfer_record",
                    make_column("id", &FileTransferRecord::id_, primary_key()),
                    make_column("the_file_id", &FileTransferRecord::the_file_id_, unique()),
                    make_column("begin", &FileTransferRecord::begin_),
                    make_column("end", &FileTransferRecord::end_),
                    make_column("visitor_device", &FileTransferRecord::visitor_device_),
                    make_column("target_device", &FileTransferRecord::target_device_),
                    make_column("direction", &FileTransferRecord::direction_),
                    make_column("file_detail", &FileTransferRecord::file_detail_),
                    make_column("success", &FileTransferRecord::success_),
                    make_column("duration", &FileTransferRecord::duration_),
                    make_column("status", &FileTransferRecord::status_, default_value(std::string("running"))),
                    make_column("end_reason", &FileTransferRecord::end_reason_, default_value(std::string())),
                    make_column("recovered", &FileTransferRecord::recovered_, default_value(false))
                ),
                make_table("audit_outbox",
                    make_column("id", &AuditOutboxRecord::id_, primary_key().autoincrement()),
                    make_column("event_key", &AuditOutboxRecord::event_key_, unique()),
                    make_column("endpoint", &AuditOutboxRecord::endpoint_),
                    make_column("payload", &AuditOutboxRecord::payload_),
                    make_column("attempts", &AuditOutboxRecord::attempts_, default_value(0)),
                    make_column("created_at", &AuditOutboxRecord::created_at_),
                    make_column("next_attempt_at", &AuditOutboxRecord::next_attempt_at_),
                    make_column("last_error", &AuditOutboxRecord::last_error_, default_value(std::string()))
                )
            );
            return st;
        }
        auto GetStorageTypeValue() {
            return InitAppDatabase("");
        }

        std::any GetDbStorage() {
            return db_storage_;
        }

        std::shared_ptr<VisitRecordOperator> GetVisitRecordOp();
        std::shared_ptr<FileTransferRecordOperator> GetFileTransferRecordOp();
        std::shared_ptr<StreamDBOperator> GetStreamDBOperator();
        std::shared_ptr<DBGameOperator> GetDBGameOperator();
        std::vector<std::shared_ptr<VisitRecord>> ScanUnclosedVisitRecords(int64_t before_timestamp);
        std::vector<std::shared_ptr<FileTransferRecord>> ScanUnclosedFileTransferRecords(int64_t before_timestamp);
        bool EnqueueAuditOutbox(const std::string& event_key, const std::string& endpoint,
                                const std::string& payload, int64_t now);
        std::optional<AuditOutboxRecord> GetDueAuditOutbox(int64_t now);
        void CompleteAuditOutbox(int64_t id);
        void RetryAuditOutbox(int64_t id, int attempts, int64_t next_attempt_at,
                              const std::string& last_error);
        int GetAuditOutboxCount();

    private:

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        std::any db_storage_;
        std::shared_ptr<VisitRecordOperator> visit_record_op_ = nullptr;
        std::shared_ptr<FileTransferRecordOperator> ft_record_op_ = nullptr;
        std::shared_ptr<StreamDBOperator> stream_operator_ = nullptr;
        std::shared_ptr<DBGameOperator> db_game_operator_ = nullptr;
        bool ready_ = false;
        std::string last_error_;
    };

}

#endif //PX_DATABASE_H
