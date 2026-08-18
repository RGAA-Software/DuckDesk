//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_PANEL_CMS_CLIENT_IMPL_H
#define PX_PANEL_CMS_CLIENT_IMPL_H

#include <memory>
#include <thread>
#include <type_traits>
#include <asio2/websocket/ws_client.hpp>
#include <asio2/websocket/wss_client.hpp>
#include "px_cms_client.h"
#include "px_common_new/concurrent_type.h"
#include "record_transfer.h"

#include "cms_panel.pb.h"
#include "px_common_new/log.h"
#include "render_panel/px_context.h"
#include "px_common_new/message_notifier.h"
#include "render_panel/px_app_messages.h"
#include "px_common_new/time_util.h"
#include "render_panel/px_settings.h"
#include "px_common_new/base64.h"
#include "px_common_new/http_client.h"
#include "records_catalog.h"
#include "records_http_handler.h"
#include "hw_info/hw_info.h"
#include <nlohmann/json.hpp>
#include "render_panel/px_application.h"
#include "render_panel/user/px_user_manager.h"
#include "network/ct_auth_token.h"

#include <chrono>
#include <filesystem>

using namespace cms_panel;

namespace px
{
    namespace fs = std::filesystem;

    // loopback / link-local addresses are useless for lan-direct access (design 5.2)
    static bool IsUsableLanIp(const std::string& ip) {
        return !ip.empty()
            && ip.rfind("127.", 0) != 0
            && ip.rfind("169.254.", 0) != 0;
    }

    // ClientType: asio2::wss_client(ssl) or asio2::ws_client(plain),
    // both share the same websocket client interface.
    template<typename ClientType>
    class PxCmsClientImpl : public PxCmsClient,
                            public std::enable_shared_from_this<PxCmsClientImpl<ClientType>> {
    public:
        explicit PxCmsClientImpl(const std::shared_ptr<PxContext>& ctx,
                                 const std::string& host,
                                 int port,
                                 const std::string& device_id) {
            settings_ = PxSettings::Instance();
            context_ = ctx;
            host_ = host;
            port_ = port;
            device_id_ = device_id;
        }

        void Start() override {
            auto weak_self = this->weak_from_this();

            msg_listener_ = context_->ObtainMessageListener();
            msg_listener_->Listen<MsgGrTimer1S>([weak_self](const MsgGrTimer1S& m) {
                auto self = weak_self.lock();
                if (!self || !self->context_) {
                    return;
                }
                self->context_->PostTask([weak_self]() {
                    auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    self->Heartbeat();
                });
            });

            msg_listener_->Listen<MsgHWInfo>([weak_self](const MsgHWInfo& info) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                self->sys_info_ = info.sys_info_;
            });

            client_ = std::make_shared<ClientType>();
            client_->set_auto_reconnect(true);
            client_->keep_alive(true);
            client_->set_timeout(std::chrono::milliseconds(3000));
            if constexpr (std::is_same_v<ClientType, asio2::wss_client>) {
                client_->set_verify_mode(asio::ssl::verify_none);
            }

            client_->bind_init([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->client_) {
                    return;
                }
                self->client_->ws_stream().binary(true);
                self->client_->set_no_delay(true);

                // Generate a fresh token for every connection attempt (including auto reconnect).
                // The token has a short lifetime (60s), so reusing the original path on reconnect
                // would cause the CMS token filter to reject the connection.
                auto user_id = grApp->GetUserManager()->GetUserId();
                auto token = GenerateConnectionToken(grApp->GetAppkey());
                auto path = std::format("/cms/panel?appkey={}&token={}&ts={}&nonce={}&device_id={}&user_id={}",
                                         grApp->GetAppkey(), token.token, token.ts, token.nonce, self->device_id_, user_id);
                self->client_->set_upgrade_target(path);
            })
            .bind_connect([weak_self]() {
                auto self = weak_self.lock();
                if (!self || !self->client_) {
                    return;
                }
                if (asio2::get_last_error()) {
                    LOGE("connect failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
                } else {
                    LOGI("connect success : {} {} ", self->client_->local_address().c_str(), self->client_->local_port());
                }

                self->client_->post_queued_event([weak_self]() {
                    auto self = weak_self.lock();
                    if (!self) {
                        return;
                    }
                    self->Hello();
                });

            })
            .bind_upgrade([]() {
                if (asio2::get_last_error()) {
                    LOGE("upgrade failure : {}, {}", asio2::last_error_val(), asio2::last_error_msg());
                }
            })
            .bind_disconnect([weak_self]() {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                LOGE("*** Disconnected for cms-client: {}", self->device_id_);
            })
            .bind_recv([weak_self](std::string_view data) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                auto msg = std::string(data.data(), data.size());
                self->ParseMessage(msg);
            });

            LOGI("will connect => {}:{}/cms/panel", host_, port_);
            if (!client_->async_start(host_, port_)) {
                LOGE("connect websocket server failure : {} {}", asio2::last_error_val(), asio2::last_error_msg().c_str());
            }

            // serial record-upload worker (design doc 6.2 / 7.2)
            fetch_queue_ = std::make_shared<RecordFetchQueue>();
            fetch_thread_ = std::thread([weak_self]() {
                auto self = weak_self.lock();
                if (self) {
                    self->FetchWorkerLoop();
                }
            });
        }

        void Stop() override {
            if (msg_listener_) {
                msg_listener_->UnListenAll();
            }
            if (fetch_queue_) {
                fetch_queue_->Stop();
            }
            if (fetch_thread_.joinable()) {
                fetch_thread_.join();
            }
            if (client_) {
                client_->stop_all_timers();
                client_->stop();
                client_.reset();
            }
        }

        bool IsStarted() override {
            return client_ != nullptr;
        }

        bool IsActive() override {
            return IsStarted() && client_->is_started();
        }

        void PostBinMessage(const std::string& m) override {
            if (IsActive()) {
                client_->async_send(m);
            }
        }

        bool IsAlive() const override {
            auto current_timestamp = TimeUtil::GetCurrentTimestamp();
            auto diff = current_timestamp - last_received_timestamp_ < 3100;
            //LOGI("Diff alive: {}", diff);
            return diff;
        }

    private:
        void Hello() {
            if (!IsActive()) {
                return;
            }
            cms_panel::CmsPanelMessage msg;
            msg.set_msg_type(cms_panel::CmsPanelMessageType::kCmsPanelHello);
            auto sub = msg.mutable_hello();
            sub->set_device_id(device_id_);
            auto user_id = grApp->GetUserManager()->GetUserId();
            sub->set_user_id(user_id);
            sub->set_device_name(settings_->GetDeviceName());

            // report local NIC IPv4 list for the cms render-records view (design 5.2);
            // the tunnel source ip is unreliable across routers/NAT
            for (const auto& eth : context_->GetIps()) {
                if (IsUsableLanIp(eth.ip_addr_)) {
                    sub->add_panel_lan_ips(eth.ip_addr_);
                }
            }
            sub->set_panel_http_port(settings_->GetPanelServerPort());
            PostBinMessage(msg.SerializeAsString());
        }

        void Heartbeat() {
            if (!IsActive()) {
                return;
            }

            auto ips = context_->GetIps();
            auto desktop_link_raw = context_->MakeDesktopLinkMessage(ips);
            auto desktop_link = std::format("link://{}", Base64::Base64Encode(desktop_link_raw));

            cms_panel::CmsPanelMessage msg;
            msg.set_msg_type(cms_panel::CmsPanelMessageType::kCmsPanelHeartBeat);
            auto sub = msg.mutable_heartbeat();
            sub->set_hb_index(hb_idx_++);
            sub->set_device_id(device_id_);
            sub->set_desktop_link(desktop_link);
            sub->set_desktop_link_raw(desktop_link_raw);
            auto user_id = grApp->GetUserManager()->GetUserId();
            sub->set_user_id(user_id);
            if (auto sys_info = sys_info_.Clone(); sys_info != nullptr) {
                try {
                    auto obj = nlohmann::json::parse(sys_info->raw_json_msg_);
                    obj["cpu"]["current_frequency"] = sys_info->cpu_.current_frequency_;
                    sub->set_sys_info_raw(obj.dump());
                }
                catch (...) {
                    sub->set_sys_info_raw(sys_info->raw_json_msg_);
                }

                //LOGI("Heartbeat sys infor raw: {}", sys_info->raw_json_msg_);
            }
            if (!ips.empty()) {
                sub->set_device_ip_addr(ips[0].ip_addr_);
            }
            sub->set_device_name(settings_->GetDeviceName());
            PostBinMessage(msg.SerializeAsString());
        }

        void ParseMessage(const std::string& m) {
            auto pm = std::make_shared<cms_panel::CmsPanelMessage>();
            bool r = pm->ParsePartialFromString(m);
            if (!r) {
                LOGE("Parse CmsClient message failed!");
                return;
            }
            last_received_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();

            auto type = pm->msg_type();
            if (type == CmsPanelMessageType::kCmsPanelHello) {
                LOGI("CmsClient hello.");
            }
            else if (type == CmsPanelMessageType::kCmsPanelHeartBeat) {
                //LOGI("CmsClient heartbeat.");
            }
            else if (type == CmsPanelMessageType::kRecordListReq) {
                if (pm->has_record_list_req()) {
                    HandleRecordListReq(pm->record_list_req());
                }
            }
            else if (type == CmsPanelMessageType::kRecordFetchReq) {
                if (pm->has_record_fetch_req()) {
                    HandleRecordFetchReq(pm->record_fetch_req());
                }
            }
        }

        void HandleRecordListReq(const cms_panel::RecordListReq& req) {
            LOGI("RecordListReq: {}", req.req_id());
            cms_panel::CmsPanelMessage msg;
            msg.set_msg_type(cms_panel::CmsPanelMessageType::kRecordListResp);
            auto sub = msg.mutable_record_list_resp();
            sub->set_device_id(device_id_);
            sub->set_req_id(req.req_id());
            try {
                for (const auto& info : ScanRecordFiles(fs::path(GetRenderRecordsDir()))) {
                    auto* f = sub->add_files();
                    f->set_name(info.name);
                    f->set_size(static_cast<int64_t>(info.size));
                    f->set_mtime(info.mtime);
                    f->set_monitor(info.monitor);
                    f->set_codec(info.codec);
                }
            }
            catch (const std::exception& e) {
                LOGE("RecordListReq scan failed: {}", e.what());
                sub->set_error(e.what());
            }
            PostBinMessage(msg.SerializeAsString());
        }

        void HandleRecordFetchReq(const cms_panel::RecordFetchReq& req) {
            LOGI("RecordFetchReq: {} -> {}", req.filename(), req.upload_url());
            if (req.filename().empty() || req.token().empty() || req.upload_url().empty()) {
                LOGE("RecordFetchReq invalid params, filename: {}", req.filename());
                return;
            }
            if (!IsValidRecordFileName(req.filename())) {
                LOGE("RecordFetchReq invalid file name: {}", req.filename());
                return;
            }
            if (!fetch_queue_) {
                LOGE("RecordFetchReq but fetch queue is not ready");
                return;
            }
            RecordFetchTask task;
            task.device_id = device_id_;
            task.req_id = req.req_id();
            task.filename = req.filename();
            task.token = req.token();
            task.upload_url = req.upload_url();
            if (!fetch_queue_->Push(task)) {
                LOGW("RecordFetchReq duplicated, ignored: {}", req.filename());
            }
        }

        void FetchWorkerLoop() {
            LOGI("record fetch worker started");
            while (fetch_queue_ && !fetch_queue_->IsStopped()) {
                RecordFetchTask task;
                if (!fetch_queue_->WaitPop(task)) {
                    break;
                }
                std::string err = UploadRecordFile(task);
                if (err.empty()) {
                    fetch_queue_->Finish(task.filename);
                    SendRecordFetchDone(task, true, "");
                    continue;
                }
                // failed: exponential backoff retry, at most kMaxAttempts tries (design 7.2)
                task.attempt += 1;
                LOGE("upload record failed (attempt {}/{}): {}, {}",
                     task.attempt, RecordFetchQueue::kMaxAttempts, task.filename, err);
                if (task.attempt >= RecordFetchQueue::kMaxAttempts) {
                    fetch_queue_->Finish(task.filename);
                    SendRecordFetchDone(task, false, err);
                    continue;
                }
                // interruptible backoff sleep
                const auto delay = RecordFetchQueue::RetryDelayMs(task.attempt);
                for (int64_t slept = 0; slept < delay && !fetch_queue_->IsStopped(); slept += 100) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                }
                fetch_queue_->Requeue(task);
            }
            LOGI("record fetch worker exited");
        }

        std::string UploadRecordFile(const RecordFetchTask& task) {
            const fs::path file_path = fs::path(GetRenderRecordsDir()) / task.filename;
            std::error_code ec;
            if (!fs::exists(file_path, ec) || !fs::is_regular_file(file_path, ec)) {
                return "file not found on device";
            }
            if (HasRecordingSidecar(file_path)) {
                return "file is still recording";
            }

            bool ssl = false;
            std::string host;
            std::string upath;
            int port = 0;
            if (!ParseUploadUrl(task.upload_url, ssl, host, port, upath)) {
                return std::format("invalid upload url: {}", task.upload_url);
            }

            const uint64_t file_size = fs::file_size(file_path, ec);
            if (ec) {
                return "stat file failed";
            }
            const int64_t mtime = FileMtimeSeconds(file_path);

            // 1GB segments over a 100Mbps link take minutes; allow 1 hour
            auto client = ssl ? HttpClient::MakeSSL(host, port, upath, 3600 * 1000)
                              : HttpClient::Make(host, port, upath, 3600 * 1000);
            auto resp = client->PostMultiPart(
                {
                    {"appkey", grApp->GetAppkey()},
                    {"token", task.token},
                    {"device_id", task.device_id},
                    {"filename", task.filename},
                    {"size", std::to_string(file_size)},
                    {"mtime", std::to_string(mtime)},
                },
                {},
                {{"file", file_path.string()}});
            if (resp.status != 200) {
                return std::format("http status: {}, err: {}", resp.status, resp.error_message);
            }
            try {
                const auto body = nlohmann::json::parse(resp.body);
                if (body.value("code", -1) != 200) {
                    return std::format("cms rejected: {}", body.value("message", std::string("unknown")));
                }
            }
            catch (const std::exception& e) {
                return std::format("bad cms response: {}", e.what());
            }
            LOGI("upload record ok: {}, {} bytes", task.filename, file_size);
            return "";
        }

        void SendRecordFetchDone(const RecordFetchTask& task, bool ok, const std::string& error) {
            cms_panel::CmsPanelMessage msg;
            msg.set_msg_type(cms_panel::CmsPanelMessageType::kRecordFetchDone);
            auto sub = msg.mutable_record_fetch_done();
            sub->set_device_id(task.device_id);
            sub->set_req_id(task.req_id);
            sub->set_filename(task.filename);
            sub->set_ok(ok);
            sub->set_error(error);
            PostBinMessage(msg.SerializeAsString());
        }

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<ClientType> client_ = nullptr;
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

#endif //PX_PANEL_CMS_CLIENT_IMPL_H
