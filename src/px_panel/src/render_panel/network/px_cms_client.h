//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_PANEL_CMS_CLIENT_H
#define PX_PANEL_CMS_CLIENT_H

#include <memory>
#include <thread>
#include <asio2/websocket/wss_client.hpp>
#include "px_common_new/concurrent_type.h"
#include "record_transfer.h"

namespace cms_panel {
    class RecordListReq;
    class RecordFetchReq;
}

namespace px
{
    class PxContext;
    class PxSettings;
    class MessageListener;
    class SysInfo;

    // Between Panel <-> Cms
    class PxCmsClient : public std::enable_shared_from_this<PxCmsClient> {
    public:
        explicit PxCmsClient(const std::shared_ptr<PxContext>& ctx,
                              const std::string& host,
                              int port,
                              const std::string& device_id);
        void Start();
        void Stop();
        bool IsStarted();
        bool IsActive();
        void PostBinMessage(const std::string& m);
        bool IsAlive() const;

    private:
        void ParseMessage(const std::string& data);
        void Hello();
        void Heartbeat();

        // record tunnel family (design doc 6.2)
        void HandleRecordListReq(const cms_panel::RecordListReq& req);
        void HandleRecordFetchReq(const cms_panel::RecordFetchReq& req);
        void FetchWorkerLoop();
        // one upload attempt; returns "" on success, error text on failure
        std::string UploadRecordFile(const RecordFetchTask& task);
        void SendRecordFetchDone(const RecordFetchTask& task, bool ok, const std::string& error);

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<asio2::wss_client> client_ = nullptr;
        std::string host_;
        int port_ = 0;
        std::string device_id_;
        std::string appkey_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::atomic_int64_t hb_idx_ = 0;
        int64_t last_received_timestamp_ = 0;
        Mutex<std::shared_ptr<SysInfo>> sys_info_;

        // serial record-upload queue (design doc 7.2)
        std::shared_ptr<RecordFetchQueue> fetch_queue_ = nullptr;
        std::thread fetch_thread_;
    };

}

#endif //PX_PANEL_CMS_CLIENT_H
