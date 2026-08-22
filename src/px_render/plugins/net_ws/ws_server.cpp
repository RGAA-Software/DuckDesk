//
// Created by RGAA on 2024/3/1.
//

#include "ws_server.h"

#include <atomic>
#include <algorithm>
#include <condition_variable>
#include <memory>
#include <filesystem>
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_common_new/data.h"
#include "px_common_new/file.h"
#include "px_common_new/folder_util.h"
#include "network/ws_media_router.h"
#include "ws_stream_router.h"
#include "ws_filetransfer_router.h"
#include "ws_user_proxy_router.h"
#include "px_render/plugin_interface/px_plugin_events.h"
#include "px_capture_new/capture_message.h"
#include "ws_plugin.h"
#include "px_common_new/url_helper.h"
#include "http_handler.h"

static std::string kUrlMedia = "/media";
static std::string kUrlFileTransfer = "/file/transfer";
static std::string kUrlUserProxy = "/user-proxy";
static std::string kUrlIpc = "/ipc";
static std::string kApiPing = "/api/ping";
static std::string kApiVerifySecurityPassword = "/verify/security/password";
static std::string kApiGetRenderConfiguration = "/get/render/configuration";
static std::string kApiPanelStreamMessage = "/panel/stream/message";
static std::string kApiAllocLocalRtc = "/alloc/local/rtc";
static std::string kUrlWebClient = "/web_client";
static std::string kUrlWebClientWildcard = "/web_client/*";

// /ipc carries raw captured frames up and user keyboard/mouse events down.
// It must only ever talk to the injected dll on the same machine.
static bool IsLoopbackAddress(const std::string& addr) {
    return addr == "127.0.0.1" || addr == "::1" || addr == "::ffff:127.0.0.1";
}

// /ipc pid 清扫用:进程是否仍存活(句柄可开且未退出)
static bool IsIpcProcessAlive(uint32_t pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h) {
        return false;
    }
    DWORD code = 0;
    const bool alive = GetExitCodeProcess(h, &code) && code == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

namespace px
{
    struct WsTicketWaitState {
        std::mutex mutex_;
        std::condition_variable cv_;
        bool completed_ = false;
        bool ok_ = false;
        std::vector<std::string> permissions_;
    };

    static bool RedeemWsFileTicket(WsPlugin* plugin,
                                   const std::unordered_map<std::string, std::string>& params) {
        const auto ticket_it = params.find("ticket");
        if (ticket_it == params.end() || ticket_it->second.empty()) return false;
        const auto nonce_it = params.find("client_nonce");
        if (nonce_it == params.end() || nonce_it->second.empty()) return false;
        auto state = std::make_shared<WsTicketWaitState>();
        auto event = std::make_shared<PxPluginRedeemConnectionTicketEvent>();
        event->ticket_ = ticket_it->second;
        event->client_nonce_ = nonce_it->second;
        event->callback_ = [state](bool ok, const std::string&, const std::vector<std::string>& permissions) {
            std::scoped_lock lock(state->mutex_);
            state->ok_ = ok;
            state->permissions_ = permissions;
            state->completed_ = true;
            state->cv_.notify_all();
        };
        plugin->CallbackEvent(event);
        std::unique_lock lock(state->mutex_);
        state->cv_.wait_for(lock, std::chrono::seconds(3), [&] { return state->completed_; });
        return state->completed_ && state->ok_
            && std::find(state->permissions_.begin(), state->permissions_.end(), "file") != state->permissions_.end();
    }

    struct aop_log {
        bool before(http::web_request &req, http::web_response &rep) {
            asio2::ignore_unused(rep);
            return true;
        }

        bool after(std::shared_ptr<asio2::http_session> &session_ptr, http::web_request &req, http::web_response &rep) {
            ASIO2_ASSERT(asio2::get_current_caller<std::shared_ptr<asio2::http_session>>().get() == session_ptr.get());
            asio2::ignore_unused(session_ptr, req, rep);
            return true;
        }
    };

    // wire 级扫描 px.Message 的 type 字段(field 10, varint, tag=0x50),
    // 识别 kVideoFrame(30)/kAudioFrame(40)——与 rtc_local_plugin.cpp 的
    // IsMediaFrameMessage 同一做法(不引 protobuf 头,避免 absl 冲突)。
    // udp_media 客户端的音视频帧都走 UDP,ws 下发前用它过滤。
    static bool IsMediaFrameMessage(const std::shared_ptr<Data>& msg) {
        if (!msg || msg->Size() < 2) {
            return false;
        }
        const auto* p = (const uint8_t*)msg->DataAddr();
        const size_t n = (size_t)msg->Size();
        size_t i = 0;
        auto read_varint = [&](uint64_t& out) -> bool {
            out = 0;
            int shift = 0;
            while (i < n && shift < 64) {
                uint8_t b = p[i++];
                out |= (uint64_t)(b & 0x7F) << shift;
                if (!(b & 0x80)) {
                    return true;
                }
                shift += 7;
            }
            return false;
        };
        // 逐字段扫描,找到 field 10(type)为止;负载按 wire type 跳过
        while (i < n) {
            uint64_t tag = 0;
            if (!read_varint(tag)) {
                return false;
            }
            const uint32_t field = (uint32_t)(tag >> 3);
            const uint32_t wire = (uint32_t)(tag & 0x7);
            if (field == 10 && wire == 0) {
                uint64_t type = 0;
                if (!read_varint(type)) {
                    return false;
                }
                // px_message.proto: kVideoFrame = 30, kAudioFrame = 40
                // udp_media 客户端的音视频都走 UDP,ws 下发前都过滤掉
                return type == 30 || type == 40;
            }
            switch (wire) {
            case 0: { uint64_t v; if (!read_varint(v)) { return false; } break; }
            case 1: i += 8; break;
            case 2: {
                uint64_t len = 0;
                if (!read_varint(len)) { return false; }
                i += (size_t)len;
                break;
            }
            case 5: i += 4; break;
            default: return false; // group 等不支持,视为非媒体帧
            }
            if (i > n) {
                return false;
            }
        }
        return false;
    }

    WsPluginServer::WsPluginServer(px::WsPlugin* plugin, uint16_t listen_port){
        this->plugin_ = plugin;
        this->listen_port_ = listen_port;
        http_handler_ = std::make_shared<HttpHandler>(plugin_);
    }

    void WsPluginServer::Start() {
        exiting_ = false;
        auto weak_self = weak_from_this();
        server_ = std::make_shared<asio2::http_server>();
        server_->bind_disconnect([weak_self](std::shared_ptr<asio2::http_session>& sess_ptr) {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
            //LOGI("client disconnected: {}", socket_fd);
            if (auto opt_val = self->stream_routers_.Remove(socket_fd); opt_val.has_value()) {
                const auto& val = opt_val.value();
                self->NotifyMediaClientDisConnected(val->conn_id_, val->stream_id_, val->visitor_device_id_, val->created_timestamp_);
                LOGI("client session removed: {}", val->visitor_device_id_);
                LOGI("App server media close, media router size: {}", self->stream_routers_.Size());
            }
            else if (self->ft_routers_.Remove(socket_fd).has_value()) {
            }
            else if (self->ipc_sessions_.Remove(socket_fd).has_value()) {
                LOGI("IPC (/ipc) session removed on disconnect, remaining={}",
                     self->ipc_sessions_.Size());
            }
        });

        server_->support_websocket(true);
        ws_data_ = std::make_shared<WsData>(WsData{
            .vars_ = {
                {"plugin",  this->plugin_},
            }
        });

        //auto exe_dir = qApp->applicationDirPath().toStdString();
        //auto pwd_file = std::format("{}/certs/password", exe_dir);
        //auto pwd = (File::OpenForRead(pwd_file))->ReadAllAsString();
        //server_->set_cert_file(
        //    "",
        //    std::format("{}/certs/server.crt", exe_dir),
        //    std::format("{}/certs/server.key", exe_dir),
        //    pwd);

        //if (asio2::get_last_error()) {
        //    LOGE("load cert files failed: {}", asio2::last_error_msg());
        //}
        //else {
        //    LOGE("set cert files success.");
        //}
        //server_->set_verify_mode(asio::ssl::verify_peer);

        // media websocket
        AddWebsocketRouter(kUrlMedia);
        AddWebsocketRouter(kUrlFileTransfer);
        // game-hook DLL (px_gh) posts CaptureVideoFrame here
        AddIpcRouter();
#if PX_USER_PROXY_ENABLED
        AddUserProxyRouter();
#endif

        // ping
        AddHttpRouter(kApiPing, [=, this](const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (!exiting_) {
                http_handler_->HandlePing(req, rep);
            }
        });

        // verify security pwd
        AddHttpRouter(kApiVerifySecurityPassword, [=, this](const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (!exiting_) {
                http_handler_->HandleVerifySecurityPassword(req, rep);
            }
        });

        // get render configuration
        AddHttpRouter(kApiGetRenderConfiguration, [=, this](const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (!exiting_) {
                http_handler_->HandleGetRenderConfiguration(req, rep);
            }
        });

        //
        AddHttpRouter(kApiPanelStreamMessage, [=, this](const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (!exiting_) {
                http_handler_->HandlePanelStreamMessage(req, rep);
            }
        });

        // kApiAllocLocalRtc
        AddHttpRouter(kApiAllocLocalRtc, [=, this](const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) {
            if (!exiting_) {
                http_handler_->HandleAllocLocalRtc(session_ptr, req, rep);
            }
        });

        // static web client pages (SPA), served from {exe_dir}/web_client
        AddWebClientRouter();

        if (listen_port_ <= 0) {
            LOGE("Listen port invalid: {}", listen_port_);
        }
        bool ret = server_->start("0.0.0.0", std::to_string(listen_port_));
        LOGI("App server start result: {}, listen port: {}", ret, listen_port_);
    }

    void WsPluginServer::Exit() {
        exiting_ = true;
        if (server_) {
            server_->stop_all_timers();
            server_->stop();
        }
    }

    void WsPluginServer::PostNetMessage(std::shared_ptr<Data> msg) {
        const bool is_media_frame = IsMediaFrameMessage(msg);
        stream_routers_.ApplyAll([=](const uint64_t& socket_fd, const std::shared_ptr<WsStreamRouter>& router) {
            // udp_media 客户端的媒体帧走 UDP 通道,ws 只发控制消息
            if (is_media_frame && router->udp_media_) {
                return;
            }
            router->PostBinaryMessage(msg);
        });
    }

    void WsPluginServer::PostIpcBinaryMessage(std::shared_ptr<Data> msg) {
        if (!msg || msg->Size() <= 0) {
            return;
        }
        const std::string payload = msg->AsString();
        int sent = 0;
        ipc_sessions_.ApplyAll([&](const uint64_t&, const std::shared_ptr<asio2::http_session>& sess) {
            if (!sess || !sess->is_started()) {
                return;
            }
            sess->async_send(payload);
            ++sent;
        });
        if (sent == 0) {
            static std::atomic<uint64_t> s_drop{0};
            const auto n = ++s_drop;
            if (n == 1 || (n % 100) == 0) {
                LOGW("PostIpcBinaryMessage: no /ipc session, drop n={} bytes={}", n, payload.size());
            }
        } else {
            static std::atomic<uint64_t> s_ok{0};
            const auto n = ++s_ok;
            if (n <= 5 || (n % 200) == 0) {
                LOGI("PostIpcBinaryMessage: sent n={} sessions={} bytes={}", n, sent, payload.size());
            }
        }
    }

    bool WsPluginServer::PostTargetStreamMessage(const std::string& stream_id, std::shared_ptr<Data> msg) {
        bool found_target_stream = false;
        const bool is_media_frame = IsMediaFrameMessage(msg);
        stream_routers_.ApplyAll([=, &found_target_stream](const uint64_t& socket_fd, const std::shared_ptr<WsStreamRouter>& router) {
            if (stream_id == router->stream_id_ || stream_id.empty()) {
                found_target_stream = true;
                // udp_media 客户端的媒体帧走 UDP 通道,ws 只发控制消息
                if (is_media_frame && router->udp_media_) {
                    return;
                }
                router->PostBinaryMessage(msg);
            }
        });
        return found_target_stream;
    }

    bool WsPluginServer::PostTargetFileTransferMessage(const std::string& stream_id, std::shared_ptr<Data> msg) {
        bool found_target_stream = false;
        ft_routers_.ApplyAll([=, &found_target_stream](const uint64_t& socket_fd, const std::shared_ptr<WsFileTransferRouter>& router) {
            if (stream_id == router->stream_id_ || stream_id.empty()) {
                router->PostBinaryMessage(msg);
                found_target_stream = true;
            }
        });
        return found_target_stream;
    }

    int WsPluginServer::GetConnectedClientsCount() {
        return (int)stream_routers_.Size();
    }

    bool WsPluginServer::IsOnlyAudioClients() {
        bool only_audio_client = true;
        stream_routers_.VisitAllCond([&](auto k, auto& v) -> bool {
            if (v->enable_video_) {
                only_audio_client = false;
                return true;
            }
            return false;
        });
        return only_audio_client;
    }

    bool WsPluginServer::IsWorking() {
        return server_ && server_->is_started();
    }

    void WsPluginServer::PostUserProxyMessage(std::shared_ptr<Data> msg) {
#if PX_USER_PROXY_ENABLED
        if (!msg) {
            return;
        }
        if (user_proxy_router_ && user_proxy_router_->IsConnected()) {
            LOGI("PostUserProxyMessage ok, len={}", msg->Size());
            user_proxy_router_->PostBinaryMessage(msg);
        } else {
            LOGW("user-proxy not connected, drop clipboard message, len={}", msg->Size());
        }
#endif
    }

    bool WsPluginServer::IsUserProxyConnected() {
#if PX_USER_PROXY_ENABLED
        return user_proxy_router_ && user_proxy_router_->IsConnected();
#else
        return false;
#endif
    }

    void WsPluginServer::AddUserProxyRouter() {
        user_proxy_router_ = WsUserProxyRouter::Make(ws_data_);
        auto weak_self = weak_from_this();
        auto weak_router = std::weak_ptr<WsUserProxyRouter>(user_proxy_router_);
        auto fn_get_socket_fd = [](std::shared_ptr<asio2::http_session> &sess_ptr) -> uint64_t {
            return (uint64_t)sess_ptr->socket().native_handle();
        };
        server_->bind(kUrlUserProxy, websocket::listener<asio2::http_session>{}
            .on("message", [weak_router, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr, std::string_view data) {
                if (auto router = weak_router.lock()) {
                    auto socket_fd = fn_get_socket_fd(sess_ptr);
                    router->OnMessage(sess_ptr, socket_fd, data);
                }
            })
            .on("open", [weak_self, weak_router, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                if (auto router = weak_router.lock()) {
                    router->OnOpen(sess_ptr);
                }
            })
            .on("close", [weak_router, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                if (auto router = weak_router.lock()) {
                    router->OnClose(sess_ptr);
                }
            })
        );
    }

    void WsPluginServer::RegisterIpcPid(uint32_t pid) {
        if (pid == 0) {
            return;
        }
        std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
        ipc_allowed_pids_.insert(pid);
        LOGI("IPC (/ipc) registered allowed pid={} (total={})", pid, ipc_allowed_pids_.size());
    }

    bool WsPluginServer::IsIpcPidAllowed(uint32_t pid) {
        std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
        return ipc_allowed_pids_.contains(pid);
    }

    void WsPluginServer::UnregisterIpcPidIfDead(uint32_t pid) {
        if (pid == 0 || IsIpcProcessAlive(pid)) {
            return;
        }
        std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
        if (ipc_allowed_pids_.erase(pid) > 0) {
            LOGI("IPC (/ipc) unregistered dead pid={} (total={})", pid, ipc_allowed_pids_.size());
        }
    }

    void WsPluginServer::SweepDeadIpcPids() {
        // On1Second 每秒驱动,每 5s 真正扫一次;集合很小,OpenProcess 开销可忽略
        if ((++ipc_pid_sweep_ticks_ % 5) != 0) {
            return;
        }
        std::vector<uint32_t> snapshot;
        {
            std::lock_guard<std::mutex> lk(ipc_pid_mtx_);
            snapshot.assign(ipc_allowed_pids_.begin(), ipc_allowed_pids_.end());
        }
        for (const auto pid : snapshot) {
            UnregisterIpcPidIfDead(pid);
        }
    }

    void WsPluginServer::AddIpcRouter() {
        // Injected px_gh.dll posts CaptureVideoFrame / IpcCaptureAudioFrame blobs.
        // Forward via plugin event bus (same path as DDA / MiniAudio) — no link to rdApp.
        auto weak_self = weak_from_this();
        server_->bind(kUrlIpc, websocket::listener<asio2::http_session>{}
            .on("message", [weak_self](std::shared_ptr<asio2::http_session> &sess_ptr, std::string_view data) {
                auto self = weak_self.lock();
                if (!self || self->exiting_ || !self->plugin_) {
                    return;
                }
                if (data.size() < sizeof(CaptureBaseMessage)) {
                    return;
                }
                // POD wire format: first field is magic for video frames.
                const auto first_u32 = *reinterpret_cast<const uint32_t*>(data.data());
                if (first_u32 == kIpcCaptureVideoFrameMagic) {
                    if (data.size() != sizeof(IpcCaptureVideoFrame)) {
                        LOGE("IPC IpcCaptureVideoFrame size mismatch: got {}, expect {}",
                             data.size(), sizeof(IpcCaptureVideoFrame));
                        return;
                    }
                    const auto* ipc_msg = reinterpret_cast<const IpcCaptureVideoFrame*>(data.data());
                    if (ipc_msg->version_ != kIpcCaptureVideoFrameVersion
                        || ipc_msg->type_ != kCaptureVideoFrame) {
                        LOGW("IPC video frame version/type mismatch: ver={} type={:#x}, drop "
                             "(render and px_gh.dll must be updated together)",
                             ipc_msg->version_, ipc_msg->type_);
                        return;
                    }
                    if (ipc_msg->frame_width_ < 16 || ipc_msg->frame_width_ > 8192
                        || ipc_msg->frame_height_ < 16 || ipc_msg->frame_height_ > 8192) {
                        static std::atomic<uint64_t> s_bad_size{0};
                        const auto n = ++s_bad_size;
                        if (n == 1 || (n % 100) == 0) {
                            LOGW("IPC video frame invalid size: {}x{}, drop n={}",
                                 ipc_msg->frame_width_, ipc_msg->frame_height_, n);
                        }
                        return;
                    }
                    auto event = std::make_shared<PxPluginCapturedVideoFrameEvent>();
                    auto& frame = event->frame_;
                    frame.capture_type_ = ipc_msg->capture_type_;
                    frame.data_length = 0;
                    frame.frame_width_ = ipc_msg->frame_width_;
                    frame.frame_height_ = ipc_msg->frame_height_;
                    frame.frame_index_ = ipc_msg->frame_index_;
                    frame.frame_format_ = ipc_msg->frame_format_;
                    frame.handle_ = ipc_msg->handle_;
                    frame.adapter_uid_ = ipc_msg->adapter_uid_;
                    memcpy(frame.display_name_, ipc_msg->display_name_, sizeof(frame.display_name_));
                    frame.display_name_[sizeof(frame.display_name_) - 1] = 0;
                    frame.monitor_index_ = ipc_msg->monitor_index_;
                    frame.left_ = ipc_msg->left_;
                    frame.top_ = ipc_msg->top_;
                    frame.right_ = ipc_msg->right_;
                    frame.bottom_ = ipc_msg->bottom_;
                    frame.request_idr_ = ipc_msg->request_idr_ != 0;
                    // raw_image_ stays null — never deserialized from the wire.
                    self->plugin_->CallbackEvent(event);
                    return;
                }
                auto base_msg = (const CaptureBaseMessage*)data.data();
                if (base_msg->type_ == kCaptureVideoFrame) {
                    // Legacy non-POD blob (old dll): refuse it, it used to memcpy a shared_ptr.
                    static std::atomic<uint64_t> s_legacy{0};
                    const auto n = ++s_legacy;
                    if (n == 1 || (n % 100) == 0) {
                        LOGW("IPC legacy CaptureVideoFrame blob rejected n={} "
                             "(upgrade px_gh.dll)", n);
                    }
                    return;
                }
                if (base_msg->type_ == kCaptureAudioFrame) {
                    if (data.size() < sizeof(IpcCaptureAudioFrame)) {
                        LOGE("IPC audio frame too small: {}", data.size());
                        return;
                    }
                    const auto* hdr = reinterpret_cast<const IpcCaptureAudioFrame*>(data.data());
                    const size_t expect = sizeof(IpcCaptureAudioFrame) + hdr->data_length;
                    if (data.size() != expect || hdr->data_length == 0) {
                        LOGE("IPC audio size mismatch: got={}, expect={}, pcm={}", data.size(),
                             expect, hdr->data_length);
                        return;
                    }
                    auto pcm = Data::Make(data.data() + sizeof(IpcCaptureAudioFrame),
                                          static_cast<int>(hdr->data_length));
                    if (!pcm) {
                        LOGE("IPC audio: Data::Make failed pcm={}", hdr->data_length);
                        return;
                    }
                    auto event = std::make_shared<PxPluginRawAudioFrameEvent>();
                    event->full_data_ = pcm;
                    event->sample_rate_ = static_cast<int>(hdr->samples_);
                    event->channels_ = static_cast<int>(hdr->channels_);
                    event->bits_ = static_cast<int>(hdr->bits_);
                    static std::atomic<uint64_t> s_audio_rx{0};
                    const auto n = ++s_audio_rx;
                    if (n == 1 || (n % 200) == 0) {
                        LOGI("IPC audio rx→RawAudioEvent: n={} idx={} {}Hz {}ch {}bit pcm={}", n,
                             hdr->frame_index_, hdr->samples_, hdr->channels_, hdr->bits_,
                             hdr->data_length);
                    }
                    self->plugin_->CallbackEvent(event);
                    return;
                }
            })
            .on("open", [weak_self](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                const std::string remote_addr(sess_ptr->remote_address().c_str());
                if (!IsLoopbackAddress(remote_addr)) {
                    // /ipc is for the injected dll only; refuse remote peers so they
                    // can neither push forged frames nor sniff the input downlink.
                    LOGW("IPC (/ipc) rejected non-loopback client {}:{}, closing",
                         remote_addr, sess_ptr->remote_port());
                    sess_ptr->stop();
                    return;
                }
                // Pid auth: the dll connects with ?pid=<its own pid>; only pids this
                // render instance wrote hook boot config for (RegisterIpcPid) are accepted.
                // This rejects stale injected games from dead renders, which otherwise
                // reconnect to whatever render starts listening and interleave frames.
                auto query = sess_ptr->get_request().get_query();
                auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));
                uint32_t client_pid = 0;
                if (auto it = params.find("pid"); it != params.end()) {
                    client_pid = static_cast<uint32_t>(std::strtoul(it->second.c_str(), nullptr, 10));
                }
                if (client_pid == 0 || !self->IsIpcPidAllowed(client_pid)) {
                    static std::atomic<uint64_t> s_reject{0};
                    const auto n = ++s_reject;
                    if (n == 1 || (n % 50) == 0) {
                        LOGW("IPC (/ipc) rejected unregistered pid={} from {}:{}, closing n={}",
                             client_pid, remote_addr, sess_ptr->remote_port(), n);
                    }
                    sess_ptr->stop();
                    return;
                }
                sess_ptr->ws_stream().binary(true);
                sess_ptr->set_no_delay(true);
                const auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
                self->ipc_sessions_.Insert(socket_fd, sess_ptr);
                self->ipc_session_pids_.Insert(socket_fd, client_pid);
                LOGI("IPC (/ipc) client connected from {}:{} fd={} pid={} sessions={}",
                     sess_ptr->remote_address().c_str(), sess_ptr->remote_port(), socket_fd,
                     client_pid, self->ipc_sessions_.Size());
            })
            .on("close", [weak_self](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self) {
                    return;
                }
                const auto socket_fd = (uint64_t)sess_ptr->socket().native_handle();
                uint32_t pid = self->ipc_session_pids_.TryGet(socket_fd).value_or(0);
                self->ipc_session_pids_.Remove(socket_fd);
                self->ipc_sessions_.Remove(socket_fd);
                // 进程已死才注销;活进程的瞬时断线靠重连恢复,注册保留
                self->UnregisterIpcPidIfDead(pid);
                LOGI("IPC (/ipc) client disconnected fd={} pid={} remaining={}", socket_fd,
                     pid, self->ipc_sessions_.Size());
            })
        );
        LOGI("Registered websocket route: {}", kUrlIpc);
    }

    void WsPluginServer::AddWebsocketRouter(const std::string &path) {
        auto weak_self = weak_from_this();
        auto fn_get_socket_fd = [](std::shared_ptr<asio2::http_session> &sess_ptr) -> uint64_t {
            auto& s = sess_ptr->socket();
            return (uint64_t)s.native_handle();
        };
        server_->bind(path, websocket::listener<asio2::http_session>{}
            .on("message", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr, std::string_view data) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                if (path == kUrlMedia) {
                    self->stream_routers_.VisitAll([=](auto k, std::shared_ptr<WsStreamRouter>& router) mutable {
                        if (socket_fd == k) {
                            router->OnMessage(sess_ptr, socket_fd, data);
                        }
                    });
                }
                else if (path == kUrlFileTransfer) {
                    self->ft_routers_.VisitAll([=](auto k, auto &router) mutable {
                        if (socket_fd == k) {
                            router->OnMessage(sess_ptr, socket_fd, data);
                        }
                    });
                }
            })
            .on("open", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto query = sess_ptr->get_request().get_query();
                auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));
                const bool standalone_file = path == kUrlFileTransfer
                    && params.contains("file_only") && params["file_only"] == "1";
                if (standalone_file && !RedeemWsFileTicket(self->plugin_, params)) {
                    LOGW("Reject file websocket: ticket is invalid or lacks file capability");
                    sess_ptr->stop();
                    return;
                }
                for (const auto& [k, v] : params) {
                    const bool sensitive = k == "ticket" || k == "appkey"
                        || k == "client_nonce" || k.find("pwd") != std::string::npos;
                    LOGI("query param, k: {}, v: {}", k, sensitive ? "<redacted>" : v);
                }
                LOGI("App server {} open", path);
                bool only_audio = std::atoi(params["only_audio"].c_str()) == 1;
                std::string server_device_id;
                std::string visitor_device_id;
                std::string stream_id;
                bool force_gdi = false;
                if (params.contains("remote_device_id")) {
                    server_device_id = params["remote_device_id"];
                }
                if (params.contains("visitor_device_id")) {
                    visitor_device_id = params["visitor_device_id"];
                }
                if (params.contains("stream_id")) {
                    stream_id = params["stream_id"];
                }
                if (params.contains("force_gdi")) {
                    force_gdi = [&]() {
                        if (auto v = params["force_gdi"]; v == "true") {
                            return true;
                        }
                        return false;
                    } ();
                }
                // udp_media=1:客户端媒体走 net_udp 插件裸 UDP 通道,
                // 本 ws 会话只承担控制面(媒体帧 proto 在下发处跳过)
                bool udp_media = false;
                if (params.contains("udp_media")) {
                    udp_media = params["udp_media"] == "1";
                }

                LOGI("Force GDI : {}", force_gdi);

                // TEST //
                if (stream_id.empty()) {
                    LOGE("!!!MUST HAVE STREAM ID!!!");
                    sess_ptr->stop();
                    return;
                }
                // TEST //

                sess_ptr->set_no_delay(true);
                auto socket_fd = fn_get_socket_fd(sess_ptr);

                if (path == kUrlMedia) {
                    // notify
                    const auto event = std::make_shared<PxPluginReqParamsBeginStreaming>();
                    event->stream_id_ = stream_id;
                    event->force_gdi_ = force_gdi;
                    self->plugin_->CallbackEvent(event);

                    auto router = WsStreamRouter::Make(self->ws_data_, only_audio, visitor_device_id, stream_id);
                    router->udp_media_ = udp_media;
                    self->stream_routers_.Insert(socket_fd, router);
                    self->NotifyMediaClientConnected(router->conn_id_, router->stream_id_, visitor_device_id);
                    router->OnOpen(sess_ptr);
                }
                else if (path == kUrlFileTransfer) {
                    auto router = WsFileTransferRouter::Make(self->ws_data_, only_audio, visitor_device_id, stream_id);
                    self->ft_routers_.Insert(socket_fd, router);
                    router->OnOpen(sess_ptr);
                }

            })
            .on("close", [weak_self, path, fn_get_socket_fd](std::shared_ptr<asio2::http_session> &sess_ptr) {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                auto socket_fd = fn_get_socket_fd(sess_ptr);
                LOGI("client closed: {}", socket_fd);
                if (path == kUrlMedia) {
                    if (auto opt_val = self->stream_routers_.Remove(socket_fd); opt_val.has_value()) {
                        const auto& val = opt_val.value();
                        self->NotifyMediaClientDisConnected(val->conn_id_, val->stream_id_, val->visitor_device_id_, val->created_timestamp_);
                        LOGI("client session removed: {}", val->visitor_device_id_);
                    }
                }
                else if (path == kUrlFileTransfer) {
                    self->ft_routers_.Remove(socket_fd);
                }
            })
            .on_ping([weak_self](auto &sess_ptr) {

            })
            .on_pong([weak_self](auto &sess_ptr) {

            })
            .on("update", [](std::shared_ptr<asio2::http_session> &sess_ptr) {
                LOGI("update");
            })
        );
    }

    void WsPluginServer::AddHttpRouter(const std::string &path,
       std::function<void(const std::string& path, std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep)>&& callback) {
        auto weak_self = weak_from_this();
        // bind it
        server_->bind<http::verb::get, http::verb::post>(path, [weak_self, path, callback = std::move(callback)](std::shared_ptr<asio2::http_session> &session_ptr, http::web_request &req, http::web_response &rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            callback(path, session_ptr, req, rep);
        }, aop_log{}); //, http::enable_cache
    }

    void WsPluginServer::AddWebClientRouter() {
        auto web_client_dir = std::filesystem::path(FolderUtil::GetCurrentFolderPath()) / "web_client";
        std::error_code ec;
        if (!std::filesystem::is_directory(web_client_dir, ec)) {
            LOGW("web client dir not found: {}, skip /web_client hosting.", web_client_dir.string());
            return;
        }

        // make rep.fill_file() resolve paths relative to the web client dir
        server_->set_root_directory(web_client_dir);

        // serve a file under the web client dir; fallback to index.html for SPA routes
        auto fn_serve = [web_client_dir](http::web_request& req, http::web_response& rep) {
            // url_path: "/web_client" or "/web_client/xxx"
            std::string url_path(req.path());
            std::string rel;
            if (url_path.size() > kUrlWebClient.size()) {
                rel = url_path.substr(kUrlWebClient.size());
                while (!rel.empty() && (rel.front() == '/' || rel.front() == '\\')) {
                    rel.erase(rel.begin());
                }
            }
            std::error_code fs_ec;
            if (rel.empty() || rel.find("..") != std::string::npos
                || !std::filesystem::is_regular_file(web_client_dir / std::filesystem::path(rel), fs_ec)) {
                rel = "index.html";
            }
            LOGI("web client request: {} => {}", url_path, rel);
            // note: asio2 detail::make_filepath appends `path` with operator+= (no separator),
            // so the path must carry a leading '/'
            rep.fill_file(std::filesystem::path("/") / rel);
        };

        auto weak_self = weak_from_this();
        // "/web_client" and "/web_client/" (trailing slashes are stripped by the router)
        server_->bind<http::verb::get>(kUrlWebClient, [weak_self, fn_serve](std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            fn_serve(req, rep);
        }, aop_log{});
        // "/web_client/xxx"
        server_->bind<http::verb::get>(kUrlWebClientWildcard, [weak_self, fn_serve](std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& rep) mutable {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            fn_serve(req, rep);
        }, aop_log{});
        LOGI("host web client pages at /web_client/, dir: {}", web_client_dir.string());
    }

    void WsPluginServer::NotifyMediaClientConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id) {
        auto event = std::make_shared<PxPluginClientConnectedEvent>();
        event->conn_id_ = conn_id;
        event->stream_id_ = stream_id;
        event->conn_type_ = "Direct";
        event->visitor_device_id_ = visitor_device_id;
        event->begin_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        this->plugin_->CallbackEvent(event);
        LOGI("Conn id: {}, device id: {}", stream_id, visitor_device_id);
    }

    void WsPluginServer::NotifyMediaClientDisConnected(const std::string& conn_id, const std::string& stream_id, const std::string& visitor_device_id, int64_t begin_timestamp) {
        auto event = std::make_shared<PxPluginClientDisConnectedEvent>();
        event->conn_id_ = conn_id;
        event->stream_id_ = stream_id;
        event->visitor_device_id_ = visitor_device_id;
        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->duration_ = event->end_timestamp_ - begin_timestamp;
        this->plugin_->CallbackEvent(event);
    }

    int64_t WsPluginServer::GetQueuingMediaMsgCount() {
        int64_t count = 0;
        stream_routers_.ApplyAll([&](const auto&, const auto& r) {
            count += r->GetQueuingMsgCount();
        });
        return count;
    }

    int64_t WsPluginServer::GetQueuingFtMsgCount() {
        int64_t count = 0;
        ft_routers_.ApplyAll([&](const auto&, const auto& r) {
            count += r->GetQueuingMsgCount();
        });
        return count;
    }

    std::vector<std::shared_ptr<PxConnectedClientInfo>> WsPluginServer::GetConnectedClientInfo() {
        std::vector<std::shared_ptr<PxConnectedClientInfo>> clients_info;
        stream_routers_.VisitAll([&](const auto&, const std::shared_ptr<WsStreamRouter>& router) {
            std::string device_name;
            {
                std::lock_guard<std::mutex> lock(router->device_name_mtx_);
                device_name = router->device_name_;
            }
            clients_info.push_back(std::make_shared<PxConnectedClientInfo>(PxConnectedClientInfo {
                .device_id_ = router->visitor_device_id_,
                .stream_id_ = router->stream_id_,
                .device_name_ = device_name,
            }));
        });
        return clients_info;
    }

    void WsPluginServer::OnClientHello(const std::shared_ptr<MsgClientHello>& event) {
        stream_routers_.VisitAll([&](const auto&, const std::shared_ptr<WsStreamRouter>& router) {
            LOGI("*** OnClientHello, evt stream id: {}, router stream id: {}, device name: {}",
                 event->stream_id_, router->stream_id_, event->device_name_);
            if (router->stream_id_ == event->stream_id_) {
                std::lock_guard<std::mutex> lock(router->device_name_mtx_);
                router->device_name_ = event->device_name_;
            }
        });
    }
}
