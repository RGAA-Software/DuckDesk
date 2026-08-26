//
// Created by RGAA on 2024-03-30.
//

#ifndef TC_SERVER_STEAM_WS_SERVER_H
#define TC_SERVER_STEAM_WS_SERVER_H

#ifndef ASIO2_ENABLE_SSL
#define ASIO2_ENABLE_SSL
#endif

#include <memory>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <asio2/asio2.hpp>
#include "px_common_new/concurrent_hashmap.h"
#include "network/ws_router.h"

namespace px
{
    class Data;
    class PxContext;
    class PxApplication;
    class HttpHandler;
    class RecordsHttpHandler;
    class PxSettings;
    class VisitRecord;
    class VisitRecordOperator;
    class FileTransferRecordOperator;
    class MessageListener;
    class PxStatistics;
    class FileTransferRecord;
    class SysInfo;

    class WSSession {
    public:
        uint64_t socket_fd_ = 0;
        int session_type_ = -1;
        std::shared_ptr<asio2::http_session> session_ = nullptr;
        std::string stream_id_;
        bool audit_registered_ = false;
    };

    class WsPanelServer : public std::enable_shared_from_this<WsPanelServer> {
    public:
        static std::shared_ptr<WsPanelServer> Make(const std::shared_ptr<PxApplication>& app);
        explicit WsPanelServer(const std::shared_ptr<PxApplication>& ctx);
        ~WsPanelServer();

        void Start();
        void Exit();
        bool IsAlive();

        // to /panel socket
        void PostPanelMessage(const std::string& msg, bool only_inner = false);
        // Send to the desktop client that owns a specific stream. Returns false
        // when there is no live matching client, so callers can launch a
        // standalone file-transfer process instead.
        bool PostPanelMessageToStream(const std::string& stream_id, const std::string& msg);

        // parse /panel socket
        bool ParsePanelMessage(uint64_t socket_fd, std::string_view msg);

        // to /panel/renderer socket
        void PostRendererMessage(std::shared_ptr<Data> msg);

        // parse /panel/renderer socket
        void ParseRendererMessage(uint64_t socket_fd, std::string_view msg);

        // parse /sys/info
        void ParseSysInfoMessage(uint64_t socket_fd, std::string_view msg);

    private:
        void AddWebsocketRouter(const std::string& path);

        void AddHttpGetRouter(const std::string& path,
           std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk);

        void AddHttpPostRouter(const std::string& path,
           std::function<void(const std::string& path, http::web_request &req, http::web_response &rep)>&& cbk);

        void RpSyncPanelInfo();

        void NotifyInsertVisitRecordToConsole(const std::shared_ptr<VisitRecord> record);

        void NotifyUpdateVisitRecordToConsole(const std::shared_ptr<VisitRecord> record);

        void NotifyInsertFileTransferRecordToConsole(const std::shared_ptr<FileTransferRecord> record);

        void NotifyUpdateFileTransferRecordToConsole(const std::shared_ptr<FileTransferRecord> record);

        // scan and close records left open by a previous crash
        void ScanAndFixUnclosedRecords();

        // deliver one durable audit event; failures remain in SQLite with backoff
        void FlushAuditOutbox();
        void PanelSocketOpened(const std::string& instance_id);
        void PanelSocketClosed(const std::shared_ptr<WSSession>& session);
        void TrackPanelTransfer(uint64_t socket_fd, const std::string& file_id, bool connected);
        void ClosePanelAuditRecordsIfOffline(const std::string& instance_id, int64_t disconnected_at);
        void RendererSocketOpened(const std::string& instance_id);
        void RendererSocketClosed(const std::shared_ptr<WSSession>& session);
        void TrackRendererVisit(uint64_t socket_fd, const std::string& conn_id, bool connected);
        void TrackRendererTransfer(uint64_t socket_fd, const std::string& file_id, bool connected);
        void CloseRendererAuditRecordsIfOffline(const std::string& instance_id, int64_t disconnected_at);

        // notify event if needed
        void NotifyEventIfNeeded(const std::shared_ptr<SysInfo>& sys_info);

    private:
        std::shared_ptr<asio2::http_server> server_ = nullptr;
        WsDataPtr ws_data_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        ConcurrentHashMap<uint64_t, std::shared_ptr<WSSession>> panel_sessions_;
        ConcurrentHashMap<uint64_t, std::shared_ptr<WSSession>> renderer_sessions_;
        std::shared_ptr<WSSession> sys_info_sess_ = nullptr;
        std::shared_ptr<HttpHandler> http_handler_ = nullptr;
        std::shared_ptr<RecordsHttpHandler> records_http_handler_ = nullptr;
        PxSettings* settings_ = nullptr;
        std::shared_ptr<VisitRecordOperator> visit_record_op_ = nullptr;
        std::shared_ptr<FileTransferRecordOperator> ft_record_op_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::shared_ptr<MessageListener> state_msg_listener_ = nullptr;
        // max speed of default ethernet
        uint64_t max_transmit_speed_ = 0;
        uint64_t max_receive_speed_ = 0;
        // statistics
        std::shared_ptr<PxStatistics> stat_ = nullptr;
        // notify once flag
        std::once_flag notify_event_flag_;
        uint64_t notify_event_count_ = 0;
        std::atomic_bool exiting_ = false;
        std::atomic_bool audit_flush_in_progress_ = false;
        std::mutex panel_audit_mtx_;
        std::unordered_map<std::string, int> panel_instance_connections_;
        std::unordered_map<std::string, std::unordered_set<std::string>> panel_transfer_ids_;
        std::mutex renderer_audit_mtx_;
        std::unordered_map<std::string, int> renderer_instance_connections_;
        std::unordered_map<std::string, std::unordered_set<std::string>> renderer_visit_ids_;
        std::unordered_map<std::string, std::unordered_set<std::string>> renderer_transfer_ids_;
    };
}

#endif //TC_SERVER_STEAM_WS_SERVER_H
