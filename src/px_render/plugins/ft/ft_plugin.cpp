//
// render 被控端文件传输插件(薄壳) — rustdesk 协议迁移阶段 2
//

#include "ft_plugin.h"

#include <filesystem>
#include <optional>
#include <vector>

#include "px_message.pb.h"
#include "ft_async_session.h"
#include "ft_engine.h"
#include "ft_path.h"
#include "transfer_job.h"
#include "px_message_new/proto_converter.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/time_util.h"
#include "px_render/plugins/plugin_ids.h"
#include "px_render/plugin_interface/px_plugin_events.h"

PX_PLUGIN_EXPORT(px::FtPlugin)

namespace px
{

    class FtPlugin::AsyncBridge final {
    public:
        explicit AsyncBridge(std::reference_wrapper<FtPlugin> owner)
            : owner_(owner) {}

        void Deactivate() {
            active_.store(false, std::memory_order_release);
        }

        FileTransferSendResult Send(const px::Message& message,
                                    const std::string& stream_id) const {
            if (!active_.load(std::memory_order_acquire)) {
                return FileTransferSendResult::Disconnected("FT plug-in bridge is inactive");
            }
            return owner_.get().SendToChannel(message, stream_id);
        }

        void Process(const std::shared_ptr<px::ft::FtEngine>& engine,
                     const std::shared_ptr<Message>& message,
                     const std::string& plugin_id,
                     const std::string& connection_id) const {
            if (active_.load(std::memory_order_acquire)) {
                owner_.get().ProcessMessage(engine, message, plugin_id, connection_id);
            }
        }

        void CompleteJob(const std::string& stream_id,
                         int32_t job_id,
                         const std::string& error) const {
            if (active_.load(std::memory_order_acquire)) {
                owner_.get().TrackJobEnd(stream_id, job_id, error);
            }
        }

        void ConfirmFallback(const std::string& stream_id,
                             int32_t job_id,
                             int32_t file_num) const {
            if (active_.load(std::memory_order_acquire)) {
                owner_.get().HandleOverwriteFallback(stream_id, job_id, file_num);
            }
        }

    private:
        std::reference_wrapper<FtPlugin> owner_;
        std::atomic_bool active_{true};
    };

    FtPlugin::~FtPlugin() = default;

    // 对齐 rustdesk check_file_count_limit 的默认上限(DEFAULT_MAX_VALIDATED_FILES,
    // ui_cm_interface.rs)。
    // TODO: 挂到设置体系 —— PxPluginSettingsInfo 暂无此字段,需 panel/SyncConfig
    // 下发链路配合,本阶段先常量。
    static constexpr size_t kMaxTransferFileCount = 10000;

    // FT 发送队列反压阈值,对齐 RtcPlugin::PostTargetFileTransferProtoMessage
    // 的水位判定(queuing > 256 或水位不足视为通道忙)。
    static constexpr int64_t kFtQueueBusyThreshold = 256;

    std::string FtPlugin::GetPluginId() {
        return kFtPluginId;
    }

    std::string FtPlugin::GetPluginName() {
        return "File Transfer";
    }

    std::string FtPlugin::GetVersionName() {
        return "1.0.0";
    }

    uint32_t FtPlugin::GetVersionCode() {
        return 100;
    }

    std::string FtPlugin::GetPluginDescription() {
        return "File transfer (rustdesk protocol, px_ft_engine core)";
    }

    void FtPlugin::On1Second() {

    }

    bool FtPlugin::OnCreate(const px::PxPluginParam& param) {
        if (!PxPluginInterface::OnCreate(param)) {
            return false;
        }

        async_runtime_ = PxAsyncRuntime::Create({.worker_threads = 1});
        if (!async_runtime_->Start()) {
            LOGE("ft async runtime failed to start.");
            return false;
        }
        async_bridge_ = std::make_shared<AsyncBridge>(std::ref(*this));
        accepting_ = true;
        LOGI("ft plugin created, shared async runtime started.");
        return true;
    }

    bool FtPlugin::OnStop() {
        accepting_ = false;
        StopSessions();
        return PxPluginInterface::OnStop();
    }

    bool FtPlugin::OnDestroy() {
        accepting_ = false;
        StopSessions();
        routes_.Clear();
        return PxPluginInterface::OnDestroy();
    }

    void FtPlugin::OnMessage(std::shared_ptr<Message> msg) {
        OnMessageRaw(FtInboundMessage{
            .message_ = std::move(msg),
            .source_plugin_id_ = {},
            .source_connection_id_ = {},
        });
    }

    void FtPlugin::OnMessageRaw(const std::any& raw) {
        if (raw.type() == typeid(FtRouteDisconnected)) {
            const auto disconnected = std::any_cast<FtRouteDisconnected>(raw);
            if (!accepting_.load()) {
                return;
            }
            ProcessRouteDisconnected(
                disconnected.stream_id_, disconnected.source_plugin_id_,
                disconnected.source_connection_id_);
            return;
        }
        if (raw.type() != typeid(FtInboundMessage)) {
            return;
        }
        const auto inbound = std::any_cast<FtInboundMessage>(raw);
        const auto& msg = inbound.message_;
        if (!msg) {
            return;
        }
        static std::atomic<uint64_t> received_count{0};
        if (msg && (msg->type() == MessageType::kFileAction
                    || msg->type() == MessageType::kFileResponse)) {
            const auto count = ++received_count;
            if (count <= 5 || (count % 500) == 0) {
                LOGI("ft plugin receive: n={}, type={}, stream={}",
                     count, static_cast<int>(msg->type()), msg->stream_id());
            }
        }
        const auto type = msg->type();
        if (type != MessageType::kFileAction && type != MessageType::kFileResponse) {
            return;
        }
        // 权限开关:入口直接拒(旧插件同款语义,rustdesk 回 "No permission of file transfer")。
        if (!sys_settings_.file_transfer_enabled_) {
            if (type == MessageType::kFileAction) {
                ReplyNoPermission(
                    msg, inbound.source_plugin_id_, inbound.source_connection_id_);
            }
            return;
        }
        // 分发线程只建立 route/session 并投递命令，不做任何磁盘 IO。
        if (!accepting_.load()) {
            return;
        }
        std::shared_ptr<px::ft::FtAsyncSession> session;
        {
            std::lock_guard route_lock(route_session_mutex_);
            if (!inbound.source_plugin_id_.empty()) {
                const auto previous = routes_.Resolve(msg->stream_id());
                const auto route = routes_.Bind(
                    msg->stream_id(), inbound.source_plugin_id_,
                    inbound.source_connection_id_);
                if (previous && previous->generation != route.generation) {
                    RetireSession(msg->stream_id(), true);
                    LOGI("FT route replaced: stream {}, generation {} -> {}, connection {}",
                         msg->stream_id(), previous->generation, route.generation,
                         inbound.source_connection_id_);
                }
            }
            session = GetOrCreateSession(msg->stream_id());
        }
        const auto weak_bridge = std::weak_ptr<AsyncBridge>(async_bridge_);
        if (!session || !session->Post(
                "ft-inbound",
                [weak_bridge, msg, plugin_id = inbound.source_plugin_id_,
                 connection_id = inbound.source_connection_id_](const auto& engine) {
                    if (const auto bridge = weak_bridge.lock()) {
                        bridge->Process(engine, msg, plugin_id, connection_id);
                    }
                })) {
            LOGW("ft inbound command rejected, stream {}", msg->stream_id());
        }
    }

    void FtPlugin::OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) {
        if (!accepting_.load()) {
            return;
        }
        LOGW("ft client disconnected, visitor: {}, stream: {}", visitor_device_id, stream_id);
        {
            std::lock_guard route_lock(route_session_mutex_);
            if (routes_.Remove(stream_id)) {
                RetireSession(stream_id, true);
            }
        }
    }

    void FtPlugin::OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings) {
        PxPluginInterface::OnSyncPluginSettingsInfo(settings);
        if (!accepting_.load()) {
            return;
        }
        const uint64_t rate_bps = settings.max_transmit_speed_ / 8;
        std::vector<std::shared_ptr<px::ft::FtAsyncSession>> sessions;
        {
            std::lock_guard lock(sessions_mutex_);
            for (const auto& [stream_id, session] : sessions_) {
                static_cast<void>(stream_id);
                sessions.push_back(session);
            }
        }
        for (const auto& session : sessions) {
            static_cast<void>(session->Post(
                "ft-rate-limit", [rate_bps](const auto& engine) {
                    engine->SetRateLimitBytesPerSec(rate_bps);
                }));
        }
    }

    // ---------------- structured async sessions ----------------

    void FtPlugin::StopSessions() {
        std::lock_guard route_lock(route_session_mutex_);
        std::vector<std::shared_ptr<px::ft::FtAsyncSession>> sessions;
        {
            std::lock_guard lock(sessions_mutex_);
            for (auto& [stream_id, session] : sessions_) {
                static_cast<void>(stream_id);
                sessions.push_back(std::move(session));
            }
            sessions_.clear();
        }
        for (const auto& session : sessions) {
            static_cast<void>(session->PostAndWait(
                "ft-cancel-before-stop",
                [](const auto& engine) {
                    std::vector<int32_t> ids;
                    for (const auto& job : engine->read_jobs()) ids.push_back(job.id());
                    for (const auto& job : engine->write_jobs()) ids.push_back(job.id());
                    for (const auto id : ids) engine->CancelJob(id);
                },
                std::chrono::seconds(5)));
            if (!session->StopAndWait(std::chrono::seconds(5))) {
                LOGE("ft async session did not stop within the unload deadline");
            }
        }
        CloseAudits("", false);
        if (async_bridge_) {
            async_bridge_->Deactivate();
        }
        if (async_runtime_) {
            async_runtime_->RequestStop();
            async_runtime_->Join();
            async_runtime_.reset();
        }
        async_bridge_.reset();
    }

    void FtPlugin::RetireSession(const std::string& stream_id, bool close_audits) {
        std::shared_ptr<px::ft::FtAsyncSession> session;
        {
            std::lock_guard lock(sessions_mutex_);
            if (const auto found = sessions_.find(stream_id); found != sessions_.end()) {
                session = std::move(found->second);
                sessions_.erase(found);
            }
        }
        if (session) {
            static_cast<void>(session->PostAndWait(
                "ft-disconnect-cleanup",
                [stream_id](const auto& engine) {
                    engine->DisconnectCleanup(stream_id);
                },
                std::chrono::seconds(5)));
            if (!session->StopAndWait(std::chrono::seconds(5))) {
                LOGE("ft session retirement timed out, stream {}", stream_id);
            }
        }
        if (close_audits) {
            CloseAudits(stream_id, false);
        }
    }

    std::shared_ptr<px::ft::FtAsyncSession> FtPlugin::GetOrCreateSession(
        const std::string& stream_id) {
        std::lock_guard lock(sessions_mutex_);
        if (const auto found = sessions_.find(stream_id); found != sessions_.end()) {
            return found->second;
        }
        if (!accepting_.load() || !async_runtime_ || !async_bridge_) {
            return {};
        }
        const auto engine = std::make_shared<px::ft::FtEngine>();
        const auto weak_bridge = std::weak_ptr<AsyncBridge>(async_bridge_);
        auto session = px::ft::FtAsyncSession::CreateOnRuntime(
            async_runtime_, engine,
            [weak_bridge, stream_id](const auto& message) {
                const auto bridge = weak_bridge.lock();
                return bridge
                    ? bridge->Send(*message, stream_id)
                    : FileTransferSendResult::Disconnected("FT plug-in bridge expired");
            },
            [weak_bridge, stream_id](const auto& configured_engine) {
                configured_engine->SetLogCallback([stream_id](const std::string& message) {
                    LOGW("[ft_engine:{}] {}", stream_id, message);
                });
                configured_engine->SetJobDoneCallback(
                    [weak_bridge, stream_id](int32_t job_id, int32_t file_num,
                                             const std::string& error) {
                        static_cast<void>(file_num);
                        if (const auto bridge = weak_bridge.lock()) {
                            bridge->CompleteJob(stream_id, job_id, error);
                        }
                    });
                configured_engine->SetOverwriteConfirmCallback(
                    [weak_bridge, stream_id](int32_t job_id, int32_t file_num,
                                             const std::string& path, bool is_upload,
                                             bool is_identical) {
                        LOGW("ft overwrite fallback, stream {}, job {}, file #{}, path {}, upload {}, identical {}",
                             stream_id, job_id, file_num, path, is_upload, is_identical);
                        if (const auto bridge = weak_bridge.lock()) {
                            bridge->ConfirmFallback(stream_id, job_id, file_num);
                        }
                    });
            },
            PxAsyncLane::kState);
        if (!session->Start()) {
            LOGE("ft session failed to start, stream {}", stream_id);
            return {};
        }
        sessions_.emplace(stream_id, session);
        return session;
    }

    void FtPlugin::ProcessMessage(const std::shared_ptr<px::ft::FtEngine>& engine,
                                  const std::shared_ptr<Message>& msg,
                                  const std::string& source_plugin_id,
                                  const std::string& source_connection_id) {
        static_cast<void>(source_plugin_id);
        static_cast<void>(source_connection_id);
        if (msg->type() == MessageType::kFileAction) {
            const auto& action = msg->file_action();
            using U = px::FileAction::UnionCase;
            if (action.union_case() == U::kReadDir) {
                LOGI("ft remote directory request: stream {}, path {}",
                     msg->stream_id(), action.read_dir().path());
            }
            if (!CheckReadPathExists(action, msg->stream_id()) || !CheckFileCountLimit(action, msg->stream_id())) {
                return;
            }
            // 审计:传输作业开始(目录操作不建作业,不入审计)。
            if (action.union_case() == U::kSend) {
                // 对端请求下载:render -> 主控
                TrackJobBegin(msg->stream_id(), action.send().id(), "Out", action.send().path(), 0, msg);
            } else if (action.union_case() == U::kReceive) {
                // 对端上传:主控 -> render
                TrackJobBegin(msg->stream_id(), action.receive().id(), "In", action.receive().path(),
                              action.receive().total_size(), msg);
            }
            engine->HandleFileAction(action, msg->stream_id());
        } else {
            engine->HandleFileResponse(msg->file_response());
        }
    }

    void FtPlugin::ProcessRouteDisconnected(
        const std::string& stream_id,
        const std::string& source_plugin_id,
        const std::string& source_connection_id) {
        std::lock_guard route_lock(route_session_mutex_);
        const bool removed = source_plugin_id.empty()
            ? routes_.Remove(stream_id)
            : (!source_connection_id.empty()
                  ? routes_.RemoveIfConnectionMatches(
                        stream_id, source_plugin_id, source_connection_id)
                  : routes_.RemoveIfPluginMatches(stream_id, source_plugin_id));
        if (!removed) {
            LOGI("Ignore stale FT transport disconnect: stream {}, route {}, connection {}",
                 stream_id, source_plugin_id, source_connection_id);
            return;
        }
        RetireSession(stream_id, true);
    }

    FileTransferSendResult FtPlugin::SendToChannel(const px::Message& msg,
                                                   const std::string& stream_id) {
        // 引擎不感知通道,type/stream_id/device_id 由壳补齐。
        px::Message out = msg;
        if (out.has_file_response()) {
            out.set_type(MessageType::kFileResponse);
        } else if (out.has_file_action()) {
            out.set_type(MessageType::kFileAction);
        }
        out.set_stream_id(stream_id);
        out.set_device_id(sys_settings_.device_id_);
        const auto route = routes_.Resolve(stream_id);
        if (!route) {
            return FileTransferSendResult::Disconnected(
                "no inbound transport route is registered for the stream");
        }
        return DispatchTargetFileTransferMessageOnRoute(
            route->plugin_id, stream_id, ProtoAsData(&out), false,
            route->connection_instance_id);
    }

    void FtPlugin::HandleOverwriteFallback(const std::string& stream_id,
                                           int32_t job_id,
                                           int32_t file_num) {
        std::shared_ptr<px::ft::FtAsyncSession> session;
        {
            std::lock_guard lock(sessions_mutex_);
            if (const auto found = sessions_.find(stream_id); found != sessions_.end()) {
                session = found->second;
            }
        }
        if (session) {
            static_cast<void>(session->Post(
                "ft-overwrite-fallback",
                [job_id, file_num](const auto& engine) {
                    engine->ConfirmFile(job_id, file_num, false, 0);
                }));
        }
    }

    // ---------------- 权限 / 上限 ----------------

    void FtPlugin::ReplyNoPermission(const std::shared_ptr<Message>& in_msg,
                                     const std::string& source_plugin_id,
                                     const std::string& source_connection_id) {
        const auto& action = in_msg->file_action();
        using U = px::FileAction::UnionCase;
        int32_t id = 0;
        int32_t file_num = -1;
        switch (action.union_case()) {
            case U::kSend:
                id = action.send().id(); file_num = action.send().file_num(); break;
            case U::kReceive:
                id = action.receive().id(); file_num = action.receive().file_num(); break;
            case U::kAllFiles:
                id = action.all_files().id(); break;
            default:
                // read_dir/create/remove/rename 等无作业语境,静默拒绝。
                LOGW("ft disabled, drop file action from stream {}", in_msg->stream_id());
                return;
        }
        px::Message out;
        out.set_type(MessageType::kFileResponse);
        out.set_stream_id(in_msg->stream_id());
        out.set_device_id(sys_settings_.device_id_);
        auto* e = out.mutable_file_response()->mutable_error();
        e->set_id(id);
        e->set_file_num(file_num);
        e->set_error("No permission of file transfer");
        if (!source_plugin_id.empty()) {
            static_cast<void>(DispatchTargetFileTransferMessageOnRoute(
                source_plugin_id, in_msg->stream_id(), ProtoAsData(&out), false,
                source_connection_id));
        }
    }

    static size_t CountEntriesRecursive(const std::string& path, size_t limit) {
        namespace fs = std::filesystem;
        std::error_code ec;
        const auto p = px::ft::ToFsPath(path);
        if (!fs::is_directory(p, ec)) {
            return fs::exists(p, ec) ? 1 : 0;
        }
        size_t count = 0;
        fs::recursive_directory_iterator it(p, fs::directory_options::skip_permission_denied, ec);
        for (const fs::recursive_directory_iterator end; !ec && it != end; it.increment(ec)) {
            if (++count > limit) {
                break;
            }
        }
        return count;
    }

    bool FtPlugin::CheckFileCountLimit(const px::FileAction& action, const std::string& stream_id) {
        using U = px::FileAction::UnionCase;
        int32_t id = 0;
        int32_t file_num = -1;
        size_t count = 0;
        switch (action.union_case()) {
            case U::kSend:
                // connection.rs:5403 - 展开后校验;这里在 worker 预扫描,超限直接拒,
                // 避免引擎递归展开超大目录。
                id = action.send().id();
                file_num = action.send().file_num();
                count = CountEntriesRecursive(action.send().path(), kMaxTransferFileCount);
                break;
            case U::kAllFiles:
                // connection.rs:3472
                id = action.all_files().id();
                count = CountEntriesRecursive(action.all_files().path(), kMaxTransferFileCount);
                break;
            case U::kReceive:
                // 对端已展开,直接数列表。
                id = action.receive().id();
                file_num = action.receive().file_num();
                count = static_cast<size_t>(action.receive().files().size());
                break;
            default:
                return true;
        }
        if (count <= kMaxTransferFileCount) {
            return true;
        }
        LOGW("ft rejected: too many files ({} exceeds limit {})", count, kMaxTransferFileCount);
        static_cast<void>(SendToChannel(
            px::ft::NewError(id, "Too many files", file_num), stream_id));
        return false;
    }

    bool FtPlugin::CheckReadPathExists(const px::FileAction& action, const std::string& stream_id) {
        using U = px::FileAction::UnionCase;
        if (action.union_case() != U::kSend) {
            return true;
        }
        std::error_code ec;
        if (std::filesystem::exists(px::ft::ToFsPath(action.send().path()), ec)) {
            return true;
        }
        LOGW("ft rejected: path not exists: {}", action.send().path());
        static_cast<void>(SendToChannel(
            px::ft::NewError(action.send().id(), "Path not exists",
                             action.send().file_num()), stream_id));
        return false;
    }

    // ---------------- 审计 ----------------

    void FtPlugin::TrackJobBegin(const std::string& stream_id, int32_t job_id, const std::string& direction,
                                 const std::string& path, uint64_t total_size,
                                 const std::shared_ptr<Message>& msg) {
        const int64_t begin_ts = (int64_t)TimeUtil::GetCurrentTimestamp();
        // panel 链路按 the_file_id 配对 Begin/End(ws_panel_server.cpp),
        // 用 路径+作业id+时间戳 保证唯一(剪贴板旧实现只 hash 路径,重复传输会撞)。
        AuditRecord rec;
        rec.the_file_id_ = MD5::Hex(path + "#" + std::to_string(job_id) + "#" + std::to_string(begin_ts));
        rec.begin_timestamp_ = begin_ts;
        rec.stream_id_ = stream_id;
        audits_[stream_id + "#" + std::to_string(job_id)] = rec;

        auto event = std::make_shared<PxPluginFileTransferBegin>();
        event->the_file_id_ = rec.the_file_id_;
        event->begin_timestamp_ = begin_ts;
        event->visitor_device_id_ = msg->device_id();
        event->direction_ = direction;
        event->file_detail_ = total_size > 0
            ? path + " (" + std::to_string(total_size) + " bytes)"
            : path;
        CallbackEvent(event);
    }

    void FtPlugin::TrackJobEnd(const std::string& stream_id, int32_t job_id, const std::string& error_or_empty) {
        auto it = audits_.find(stream_id + "#" + std::to_string(job_id));
        if (it == audits_.end()) {
            return;
        }
        auto event = std::make_shared<PxPluginFileTransferEnd>();
        event->the_file_id_ = it->second.the_file_id_;
        event->end_timestamp_ = (int64_t)TimeUtil::GetCurrentTimestamp();
        event->duration_ = event->end_timestamp_ - it->second.begin_timestamp_;
        event->success_ = error_or_empty.empty();
        CallbackEvent(event);
        audits_.erase(it);
    }

    void FtPlugin::CloseAudits(const std::string& stream_id, bool success) {
        // 断线/停止时关闭悬挂记录,避免 panel 侧留下只有 Begin 的记录。
        // stream_id 非空时只关该连接的,不影响其他连接在途作业的记录。
        std::vector<std::pair<std::string, int32_t>> ids;
        for (const auto& [key, rec] : audits_) {
            if (stream_id.empty() || rec.stream_id_ == stream_id) {
                const auto pos = key.rfind('#');
                if (pos != std::string::npos) ids.emplace_back(rec.stream_id_, std::stoi(key.substr(pos + 1)));
            }
        }
        for (const auto& [id_stream, id] : ids) {
            TrackJobEnd(id_stream, id, success ? "" : "interrupted");
        }
    }

}
