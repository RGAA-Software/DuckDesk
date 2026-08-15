//
// Created by RGAA on 29/05/2025.
//

#ifndef GAMMARAY_GR_DATABASE_H
#define GAMMARAY_GR_DATABASE_H

#include <any>
#include <memory>
extern "C" {
#include <sqlite3.h>
}
#include <sqlite_orm/sqlite_orm.h>
#include "db_game.h"
#include "visit_record.h"
#include "file_transfer_record.h"
#include "px_cms_client/cms_stream.h"

using namespace sqlite_orm;

namespace px
{

    class GrContext;
    class StreamDBOperator;
    class DBGameOperator;
    class VisitRecordOperator;
    class FileTransferRecordOperator;

    class GrDatabase : public std::enable_shared_from_this<GrDatabase> {
    public:
        explicit GrDatabase(const std::shared_ptr<GrContext>& ctx);
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
                    make_column("id", &px_cms::CmsStream::_id, primary_key()),
                    make_column("stream_id", &px_cms::CmsStream::stream_id_),
                    make_column("stream_name", &px_cms::CmsStream::stream_name_),
                    make_column("encode_bps", &px_cms::CmsStream::encode_bps_),
                    make_column("audio_enabled", &px_cms::CmsStream::audio_enabled_, default_value(0)),
                    make_column("clipboard_enabled", &px_cms::CmsStream::clipboard_enabled_, default_value(0)),
                    make_column("only_viewing", &px_cms::CmsStream::only_viewing_, default_value(0)),
                    make_column("show_max_window", &px_cms::CmsStream::auto_layout_screens_, default_value(0)),
                    make_column("split_windows", &px_cms::CmsStream::split_windows_, default_value(0)),
                    make_column("audio_capture_mode", &px_cms::CmsStream::audio_capture_mode_),
                    make_column("stream_host", &px_cms::CmsStream::stream_host_),
                    make_column("stream_port", &px_cms::CmsStream::stream_port_),
                    make_column("relay_host", &px_cms::CmsStream::relay_host_, default_value("")),
                    make_column("relay_port", &px_cms::CmsStream::relay_port_, default_value(0)),
                    //make_column("relay_appkey", &px_cms::CmsStream::relay_appkey_, default_value("")),
                    make_column("bg_color", &px_cms::CmsStream::bg_color_),
                    make_column("encode_fps", &px_cms::CmsStream::encode_fps_),
                    make_column("connect_type", &px_cms::CmsStream::connect_type_),
                    make_column("device_id", &px_cms::CmsStream::device_id_),
                    make_column("device_random_pwd", &px_cms::CmsStream::device_random_pwd_),
                    make_column("device_safety_pwd", &px_cms::CmsStream::device_safety_pwd_),
                    make_column("remote_device_id", &px_cms::CmsStream::remote_device_id_),
                    make_column("remote_device_random_pwd", &px_cms::CmsStream::remote_device_random_pwd_),
                    make_column("remote_device_safety_pwd", &px_cms::CmsStream::remote_device_safety_pwd_),
                    make_column("created_timestamp", &px_cms::CmsStream::created_timestamp_, default_value(0)),
                    make_column("updated_timestamp", &px_cms::CmsStream::updated_timestamp_, default_value(0)),
                    make_column("enable_p2p", &px_cms::CmsStream::enable_p2p_, default_value(0)),
                    make_column("desktop_name", &px_cms::CmsStream::desktop_name_),
                    make_column("os_version", &px_cms::CmsStream::os_version_),
                    make_column("force_relay", &px_cms::CmsStream::force_relay_, default_value(false)),
                    make_column("force_direct", &px_cms::CmsStream::force_direct_, default_value(false)),
                    make_column("force_software", &px_cms::CmsStream::force_software_, default_value(false)),
                    make_column("wait_debug", &px_cms::CmsStream::wait_debug_, default_value(false)),
                    make_column("force_gdi_capture", &px_cms::CmsStream::force_gdi_capture_, default_value(false)),
                    make_column("disable_vulkan_render", &px_cms::CmsStream::disable_vulkan_render_, default_value(false)),
                    make_column("use_webrtc", &px_cms::CmsStream::use_webrtc_, default_value(true)),
                    make_column("use_udp", &px_cms::CmsStream::use_udp_, default_value(false))
                ),
                make_table("visit_record",
                    make_column("id", &VisitRecord::id_, primary_key()),
                    make_column("stream_id", &VisitRecord::stream_id_),
                    make_column("conn_id", &VisitRecord::conn_id_, unique()),
                    make_column("conn_type", &VisitRecord::conn_type_),
                    make_column("begin", &VisitRecord::begin_),
                    make_column("end", &VisitRecord::end_),
                    make_column("duration", &VisitRecord::duration_),
                    make_column("visitor_device", &VisitRecord::visitor_device_),
                    make_column("target_device", &VisitRecord::target_device_)
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
                    make_column("duration", &FileTransferRecord::duration_)
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

    private:

    private:
        std::shared_ptr<GrContext> context_ = nullptr;
        std::any db_storage_;
        std::shared_ptr<VisitRecordOperator> visit_record_op_ = nullptr;
        std::shared_ptr<FileTransferRecordOperator> ft_record_op_ = nullptr;
        std::shared_ptr<StreamDBOperator> stream_operator_ = nullptr;
        std::shared_ptr<DBGameOperator> db_game_operator_ = nullptr;
        bool ready_ = false;
        std::string last_error_;
    };

}

#endif //GAMMARAY_GR_DATABASE_H
