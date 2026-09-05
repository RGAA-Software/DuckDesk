//
// render 被控端文件传输插件(薄壳) — rustdesk 协议迁移阶段 2
//

#include "architecture/services/file_transfer_service.h"

#include <filesystem>
#include <optional>
#include <vector>

#include "px_message.pb.h"
#include "ft_async_session.h"
#include "ft_engine.h"
#include "ft_path.h"
#include "ft_terminal.h"
#include "transfer_job.h"
#include "px_message_new/proto_converter.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/privacy_log.h"
#include "px_common_new/time_util.h"
#include "architecture/services/file_transfer_types.h"

namespace px::render
{

    class FileTransferService::AsyncBridge final {
    public:
        explicit AsyncBridge(std::weak_ptr<FileTransferService> owner)
            : owner_(std::move(owner)) {}

        void Deactivate() {
            active_.store(false, std::memory_order_release);
        }

        FileTransferSendResult Send(const std::string& logical_session_id,
                                    const px::Message& message,
                                    const std::string& stream_id) const {
            if (!active_.load(std::memory_order_acquire)) {
                return FileTransferSendResult::Disconnected("FT plug-in bridge is inactive");
            }
            const auto owner = owner_.lock();
            return owner
                ? owner->SendToChannel(logical_session_id, message, stream_id)
                : FileTransferSendResult::Disconnected(
                      "FT service owner expired");
        }

        void Process(const std::shared_ptr<px::ft::FtEngine>& engine,
                     const std::shared_ptr<Message>& message,
                     const std::string& logical_session_id,
                     const std::string& plugin_id,
                     const std::string& connection_id) const {
            if (const auto owner = owner_.lock();
                active_.load(std::memory_order_acquire) && owner) {
                owner->ProcessMessage(
                    engine, message, logical_session_id, plugin_id, connection_id);
            }
        }

        void CompleteJob(const std::string& logical_session_id, const std::string& stream_id,
                         int32_t job_id,
                         const std::string& error) const {
            if (const auto owner = owner_.lock();
                active_.load(std::memory_order_acquire) && owner) {
                owner->TrackJobEnd(logical_session_id, stream_id, job_id, error);
            }
        }

        void ConfirmFallback(const std::string& logical_session_id, const std::string& stream_id,
                             int32_t job_id,
                             int32_t file_num) const {
            if (const auto owner = owner_.lock();
                active_.load(std::memory_order_acquire) && owner) {
                owner->HandleOverwriteFallback(
                    logical_session_id, stream_id, job_id, file_num);
            }
        }

    private:
        std::weak_ptr<FileTransferService> owner_;
        std::atomic_bool active_{true};
    };

    std::shared_ptr<FileTransferService> FileTransferService::Create(
        FileTransferServiceOptions options,
        SendCallback send_callback,
        AuditBeginCallback audit_begin_callback,
        AuditEndCallback audit_end_callback) {
        return std::make_shared<FileTransferService>(
            std::move(options), std::move(send_callback),
            std::move(audit_begin_callback), std::move(audit_end_callback));
    }

    FileTransferService::FileTransferService(
        FileTransferServiceOptions options,
        SendCallback send_callback,
        AuditBeginCallback audit_begin_callback,
        AuditEndCallback audit_end_callback)
        : options_(std::move(options)),
          send_callback_(std::move(send_callback)),
          audit_begin_callback_(std::move(audit_begin_callback)),
          audit_end_callback_(std::move(audit_end_callback)),
          enabled_(options_.enabled) {}

    FileTransferService::~FileTransferService() {
        static_cast<void>(Stop());
    }

    // 对齐 rustdesk check_file_count_limit 的默认上限(DEFAULT_MAX_VALIDATED_FILES,
    // ui_cm_interface.rs)。
    // TODO: Add the corresponding field to RenderRuntimeSettings and the Panel synchronization protocol.
    // 下发链路配合,本阶段先常量。
    static constexpr size_t kMaxTransferFileCount = 10000;

    // FT 发送队列反压阈值,对齐 WebRTC network library 的文件传输发送路径。
    // 的水位判定(queuing > 256 或水位不足视为通道忙)。
    static constexpr int64_t kFtQueueBusyThreshold = 256;

    BuiltinModuleRegistration FileTransferService::MakeRegistration() {
        const std::weak_ptr<FileTransferService> weak_owner = weak_from_this();
        return BuiltinModuleRegistration{
            .descriptor = BuiltinModuleDescriptor{
                .id = std::string(kFileTransferModuleId),
                .name = "File Transfer",
                .author = "GammaRay",
                .description = "Built-in routed asynchronous file-transfer service",
                .version_name = "2.0.0",
                .version_code = 200,
                .capability = BuiltinModuleCapability::kService,
                .default_enabled = options_.enabled,
            },
            .start = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
                const auto owner = weak_owner.lock();
                co_return owner ? owner->Start() : ModuleLifecycleResult{};
            },
            .stop = [weak_owner]() -> PxAwaitable<ModuleLifecycleResult> {
                const auto owner = weak_owner.lock();
                co_return owner ? owner->Stop() : ModuleLifecycleResult{};
            },
            .set_enabled = [weak_owner](const bool enabled) {
                const auto owner = weak_owner.lock();
                return owner ? owner->SetEnabled(enabled)
                             : ModuleLifecycleResult{};
            },
        };
    }

    ModuleLifecycleResult FileTransferService::Start() {
        if (accepting_.exchange(true)) {
            return {};
        }
        async_runtime_ = PxAsyncRuntime::Create({.worker_threads = 1});
        if (!async_runtime_->Start()) {
            accepting_ = false;
            return std::unexpected(RenderError{
                .code = RenderErrorCode::kModuleStartFailed,
                .component = "file_transfer",
                .operation = "start",
                .stage = "service",
                .reason = "FT async runtime failed to start",
                .recoverable = true,
            });
        }
        async_bridge_ = std::make_shared<AsyncBridge>(weak_from_this());
        UpdateRateLimit(options_.max_transmit_speed_bits_per_second);
        LOGI("event=module.start component=file_transfer operation=start "
             "outcome=success recoverable=true");
        return {};
    }

    ModuleLifecycleResult FileTransferService::Stop() {
        if (!accepting_.exchange(false) && !async_runtime_) {
            return {};
        }
        StopSessions();
        routes_.Clear();
        LOGI("event=module.stop component=file_transfer operation=drain "
             "outcome=success recoverable=true");
        return {};
    }

    ModuleLifecycleResult FileTransferService::SetEnabled(const bool enabled) {
        enabled_ = enabled;
        return {};
    }

    void FileTransferService::HandleRouteDisconnected(
        const FileTransferRouteDisconnected& disconnected) {
        if (!accepting_.load()) {
            return;
        }
        ProcessRouteDisconnected(
            disconnected.logical_session_id, disconnected.stream_id,
            disconnected.transport_id, disconnected.connection_id);
    }

    void FileTransferService::HandleInbound(
        const FileTransferInbound& inbound) {
        const auto& msg = inbound.message;
        if (!msg) {
            return;
        }
        static std::atomic<uint64_t> received_count{0};
        if (msg && (msg->type() == MessageType::kFileAction
                    || msg->type() == MessageType::kFileResponse)) {
            const auto count = ++received_count;
            if (count <= 5 || (count % 500) == 0) {
                LOGD("event=service.message component=file_transfer operation=receive "
                     "outcome=accepted count={} type={} stream={}",
                     count, static_cast<int>(msg->type()),
                     PrivacyLogId(msg->stream_id()));
            }
        }
        const auto type = msg->type();
        if (type != MessageType::kFileAction && type != MessageType::kFileResponse) {
            return;
        }
        // 权限开关:入口直接拒(旧插件同款语义,rustdesk 回 "No permission of file transfer")。
        if (!enabled_.load()) {
            ++rejected_messages_;
            if (type == MessageType::kFileAction) {
                ReplyNoPermission(
                    msg, inbound.transport_id, inbound.connection_id);
            }
            return;
        }
        // 分发线程只建立 route/session 并投递命令，不做任何磁盘 IO。
        if (!accepting_.load()) {
            ++rejected_messages_;
            return;
        }
        ++accepted_messages_;
        std::shared_ptr<px::ft::FtAsyncSession> session;
        {
            std::lock_guard route_lock(route_session_mutex_);
            if (!inbound.transport_id.empty()) {
                const auto previous = routes_.Resolve(
                    inbound.logical_session_id, msg->stream_id());
                const auto route = routes_.Bind(
                    inbound.logical_session_id, msg->stream_id(),
                    inbound.transport_id, inbound.connection_id);
                if (previous && previous->generation != route.generation) {
                    RetireSession(inbound.logical_session_id, msg->stream_id(), true);
                    LOGI("event=transport.route_replace component=file_transfer "
                         "operation=bind outcome=success stream={} connection={} "
                         "old_generation={} generation={}",
                         PrivacyLogId(msg->stream_id()),
                         PrivacyLogId(inbound.connection_id),
                         previous->generation, route.generation);
                }
            }
            session = GetOrCreateSession(inbound.logical_session_id, msg->stream_id());
        }
        const auto weak_bridge = std::weak_ptr<AsyncBridge>(async_bridge_);
        if (!session || !session->Post(
                "ft-inbound",
                [weak_bridge, msg, logical_session_id = inbound.logical_session_id,
                 plugin_id = inbound.transport_id,
                 connection_id = inbound.connection_id](const auto& engine) {
                    if (const auto bridge = weak_bridge.lock()) {
                        try {
                            bridge->Process(
                                engine, msg, logical_session_id, plugin_id, connection_id);
                        } catch (const std::exception& error) {
                            LOGE("event=service.command component=file_transfer "
                                 "code=FILE_TRANSFER_COMMAND_FAILED operation=process "
                                 "outcome=failed recoverable=true stream={} type={} "
                                 "exception_type=standard",
                                 PrivacyLogId(msg->stream_id()),
                                 static_cast<int>(msg->type()));
                        } catch (...) {
                            LOGE("event=service.command component=file_transfer "
                                 "code=FILE_TRANSFER_COMMAND_FAILED operation=process "
                                 "outcome=failed recoverable=true stream={} type={} "
                                 "exception_type=unknown",
                                 PrivacyLogId(msg->stream_id()),
                                 static_cast<int>(msg->type()));
                        }
                    }
                })) {
            LOGW("event=service.command component=file_transfer "
                 "code=FILE_TRANSFER_QUEUE_CLOSED operation=enqueue "
                 "outcome=rejected recoverable=true stream={}",
                 PrivacyLogId(msg->stream_id()));
        }
    }

    void FileTransferService::HandleClientDisconnected(
        const std::string& visitor_device_id,
        const std::string& stream_id) {
        if (!accepting_.load()) {
            return;
        }
        LOGI("event=session.close component=file_transfer operation=disconnect "
             "outcome=success visitor={} stream={}",
             PrivacyLogId(visitor_device_id), PrivacyLogId(stream_id));
        {
            std::lock_guard route_lock(route_session_mutex_);
            if (routes_.Remove(stream_id)) {
                RetireSession({}, stream_id, true);
            }
        }
    }

    void FileTransferService::UpdateRateLimit(
        const std::uint64_t max_transmit_speed_bits_per_second) {
        options_.max_transmit_speed_bits_per_second =
            max_transmit_speed_bits_per_second;
        if (!accepting_.load()) {
            return;
        }
        const uint64_t rate_bps = max_transmit_speed_bits_per_second / 8;
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

    void FileTransferService::StopSessions() {
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
                LOGE("event=async.scope_drain component=file_transfer "
                     "code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=stop "
                     "outcome=failed recoverable=false deadline_ms=5000");
            }
        }
        CloseAudits(std::nullopt, {}, false);
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

    void FileTransferService::RetireSession(const std::string& logical_session_id,
                                 const std::string& stream_id, bool close_audits) {
        const auto session_key = MakeSessionKey(logical_session_id, stream_id);
        std::shared_ptr<px::ft::FtAsyncSession> session;
        {
            std::lock_guard lock(sessions_mutex_);
            if (const auto found = sessions_.find(session_key); found != sessions_.end()) {
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
            const auto statistics = session->GetStatistics();
            LOGI("event=transport.window component=file_transfer operation=session_close "
                 "outcome=success stream={} accepted={} busy={} disconnected={} "
                 "transport_errors={} writable_waits={} writable_wakeups={} "
                 "writable_closures={} writable_timeouts={} writable_interruptions={}",
                 PrivacyLogId(stream_id),
                 statistics.accepted_messages,
                 statistics.busy_retries,
                 statistics.disconnected_retries,
                 statistics.transport_errors,
                 statistics.writable_waits,
                 statistics.writable_wakeups,
                 statistics.writable_closures,
                 statistics.writable_timeouts,
                 statistics.writable_interruptions);
            if (!session->StopAndWait(std::chrono::seconds(5))) {
                LOGE("event=async.scope_drain component=file_transfer "
                     "code=ASYNC_SCOPE_DRAIN_TIMEOUT operation=retire_session "
                     "outcome=failed recoverable=false stream={} deadline_ms=5000",
                     PrivacyLogId(stream_id));
            }
        }
        if (close_audits) {
            CloseAudits(logical_session_id, stream_id, false);
        }
    }

    std::shared_ptr<px::ft::FtAsyncSession> FileTransferService::GetOrCreateSession(
        const std::string& logical_session_id, const std::string& stream_id) {
        const auto session_key = MakeSessionKey(logical_session_id, stream_id);
        std::lock_guard lock(sessions_mutex_);
        if (const auto found = sessions_.find(session_key); found != sessions_.end()) {
            return found->second;
        }
        if (!accepting_.load() || !async_runtime_ || !async_bridge_) {
            return {};
        }
        const auto engine = std::make_shared<px::ft::FtEngine>();
        const auto weak_bridge = std::weak_ptr<AsyncBridge>(async_bridge_);
        auto session = px::ft::FtAsyncSession::CreateOnRuntime(
            async_runtime_, engine,
            [weak_bridge, logical_session_id, stream_id](const auto& message) {
                const auto bridge = weak_bridge.lock();
                return bridge
                    ? bridge->Send(logical_session_id, *message, stream_id)
                    : FileTransferSendResult::Disconnected("FT plug-in bridge expired");
            },
            [weak_bridge, logical_session_id, stream_id](const auto& configured_engine) {
                configured_engine->SetLogCallback([logical_session_id, stream_id](
                                                   const std::string& message) {
                    static_cast<void>(message);
                    LOGW("event=service.engine component=file_transfer "
                         "code=FILE_TRANSFER_ENGINE_WARNING operation=process "
                         "outcome=degraded recoverable=true session={} stream={}",
                         PrivacyLogId(logical_session_id),
                         PrivacyLogId(stream_id));
                });
                configured_engine->SetJobDoneCallback(
                    [weak_bridge, logical_session_id, stream_id](int32_t job_id, int32_t file_num,
                                             const std::string& error) {
                        static_cast<void>(file_num);
                        if (const auto bridge = weak_bridge.lock()) {
                            bridge->CompleteJob(logical_session_id, stream_id, job_id, error);
                        }
                    });
                configured_engine->SetOverwriteConfirmCallback(
                    [weak_bridge, logical_session_id, stream_id](int32_t job_id, int32_t file_num,
                                             const std::string& path, bool is_upload,
                                             bool is_identical) {
                        static_cast<void>(path);
                        LOGW("event=service.overwrite component=file_transfer "
                             "code=FILE_TRANSFER_OVERWRITE_CONFIRM_REQUIRED "
                             "operation=confirm outcome=fallback recoverable=true "
                             "session={} stream={} job={} file_num={} upload={} identical={}",
                             PrivacyLogId(logical_session_id),
                             PrivacyLogId(stream_id), job_id, file_num,
                             is_upload, is_identical);
                        if (const auto bridge = weak_bridge.lock()) {
                            bridge->ConfirmFallback(
                                logical_session_id, stream_id, job_id, file_num);
                        }
                    });
            },
            PxAsyncLane::kState);
        if (!session->Start()) {
            LOGE("event=module.start component=file_transfer "
                 "code=FILE_TRANSFER_SESSION_START_FAILED operation=start_session "
                 "outcome=failed recoverable=true stream={}",
                 PrivacyLogId(stream_id));
            return {};
        }
        sessions_.emplace(session_key, session);
        return session;
    }

    void FileTransferService::ProcessMessage(const std::shared_ptr<px::ft::FtEngine>& engine,
                                  const std::shared_ptr<Message>& msg,
                                  const std::string& logical_session_id,
                                  const std::string& source_plugin_id,
                                  const std::string& source_connection_id) {
        static_cast<void>(source_plugin_id);
        static_cast<void>(source_connection_id);
        if (msg->type() == MessageType::kFileAction) {
            const auto& action = msg->file_action();
            using U = px::FileAction::UnionCase;
            if (action.union_case() == U::kReadDir) {
                LOGD("event=service.directory component=file_transfer "
                     "operation=read outcome=requested stream={}",
                     PrivacyLogId(msg->stream_id()));
            }
            if (!CheckReadPathExists(action, logical_session_id, msg->stream_id())
                || !CheckFileCountLimit(action, logical_session_id, msg->stream_id())) {
                return;
            }
            // 审计:传输作业开始(目录操作不建作业,不入审计)。
            if (action.union_case() == U::kSend) {
                // 对端请求下载:render -> 主控
                TrackJobBegin(logical_session_id, msg->stream_id(), action.send().id(),
                              "Out", action.send().path(), 0, msg);
            } else if (action.union_case() == U::kReceive) {
                // 对端上传:主控 -> render
                TrackJobBegin(logical_session_id, msg->stream_id(), action.receive().id(),
                              "In", action.receive().path(),
                              action.receive().total_size(), msg);
            }
            engine->HandleFileAction(action, msg->stream_id());
        } else {
            engine->HandleFileResponse(msg->file_response());
        }
    }

    void FileTransferService::ProcessRouteDisconnected(
        const std::string& logical_session_id,
        const std::string& stream_id,
        const std::string& source_plugin_id,
        const std::string& source_connection_id) {
        std::lock_guard route_lock(route_session_mutex_);
        const bool removed = source_plugin_id.empty()
            ? routes_.Remove(logical_session_id, stream_id)
            : (!source_connection_id.empty()
                  ? routes_.RemoveIfConnectionMatches(logical_session_id,
                        stream_id, source_plugin_id, source_connection_id)
                  : routes_.RemoveIfPluginMatches(
                        logical_session_id, stream_id, source_plugin_id));
        if (!removed) {
            LOGI("event=transport.disconnect component=file_transfer "
                 "operation=retire_route outcome=ignored_stale stream={} "
                 "transport={} connection={}",
                 PrivacyLogId(stream_id), PrivacyLogId(source_plugin_id),
                 PrivacyLogId(source_connection_id));
            return;
        }
        RetireSession(logical_session_id, stream_id, true);
    }

    FileTransferSendResult FileTransferService::SendToChannel(
        const std::string& logical_session_id,
        const px::Message& msg,
        const std::string& stream_id) {
        // 引擎不感知通道,type/stream_id/device_id 由壳补齐。
        px::Message out = msg;
        if (out.has_file_response()) {
            out.set_type(MessageType::kFileResponse);
        } else if (out.has_file_action()) {
            out.set_type(MessageType::kFileAction);
        }
        out.set_stream_id(stream_id);
        out.set_device_id(options_.device_id);
        const auto route = routes_.Resolve(logical_session_id, stream_id);
        if (!route) {
            return FileTransferSendResult::Disconnected(
                "no inbound transport route is registered for the stream");
        }
        if (!send_callback_) {
            return FileTransferSendResult::Disconnected(
                "file-transfer transport callback is unavailable");
        }
        return send_callback_(route->plugin_id, stream_id, ProtoAsData(&out),
                              route->connection_instance_id);
    }

    void FileTransferService::HandleOverwriteFallback(
        const std::string& logical_session_id,
        const std::string& stream_id,
        int32_t job_id,
        int32_t file_num) {
        const auto session_key = MakeSessionKey(logical_session_id, stream_id);
        std::shared_ptr<px::ft::FtAsyncSession> session;
        {
            std::lock_guard lock(sessions_mutex_);
            if (const auto found = sessions_.find(session_key); found != sessions_.end()) {
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

    void FileTransferService::ReplyNoPermission(
        const std::shared_ptr<Message>& in_msg,
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
                LOGW("event=service.command component=file_transfer "
                     "code=FILE_TRANSFER_PERMISSION_DENIED operation=dispatch "
                     "outcome=rejected recoverable=true stream={}",
                     PrivacyLogId(in_msg->stream_id()));
                return;
        }
        px::Message out;
        out.set_type(MessageType::kFileResponse);
        out.set_stream_id(in_msg->stream_id());
        out.set_device_id(options_.device_id);
        auto& error = *out.mutable_file_response()->mutable_error();
        error.set_id(id);
        error.set_file_num(file_num);
        error.set_error("No permission of file transfer");
        if (!source_plugin_id.empty() && send_callback_) {
            static_cast<void>(send_callback_(
                source_plugin_id, in_msg->stream_id(), ProtoAsData(&out),
                source_connection_id));
        }
    }

    bool FileTransferService::CheckFileCountLimit(
        const px::FileAction& action,
        const std::string& logical_session_id,
        const std::string& stream_id) {
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
                count = px::ft::CountRecursiveRegularFiles(
                    action.send().path(), kMaxTransferFileCount);
                break;
            case U::kAllFiles:
                // connection.rs:3472
                id = action.all_files().id();
                count = px::ft::CountRecursiveRegularFiles(
                    action.all_files().path(), kMaxTransferFileCount);
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
        LOGW("event=service.command component=file_transfer "
             "code=FILE_TRANSFER_FILE_COUNT_LIMIT operation=validate "
             "outcome=rejected recoverable=true count={} limit={} stream={}",
             count, kMaxTransferFileCount, PrivacyLogId(stream_id));
        static_cast<void>(SendToChannel(
            logical_session_id, px::ft::NewError(id, "Too many files", file_num), stream_id));
        return false;
    }

    bool FileTransferService::CheckReadPathExists(
        const px::FileAction& action,
        const std::string& logical_session_id,
        const std::string& stream_id) {
        using U = px::FileAction::UnionCase;
        if (action.union_case() != U::kSend) {
            return true;
        }
        std::error_code ec;
        if (std::filesystem::exists(px::ft::ToFsPath(action.send().path()), ec)) {
            return true;
        }
        LOGW("event=service.command component=file_transfer "
             "code=FILE_TRANSFER_PATH_NOT_FOUND operation=validate "
             "outcome=rejected recoverable=true stream={}",
             PrivacyLogId(stream_id));
        static_cast<void>(SendToChannel(
            logical_session_id, px::ft::NewError(action.send().id(), "Path not exists",
                             action.send().file_num()), stream_id));
        return false;
    }

    // ---------------- 审计 ----------------

    void FileTransferService::TrackJobBegin(
        const std::string& logical_session_id,
        const std::string& stream_id, int32_t job_id,
        const std::string& direction,
        const std::string& path, uint64_t total_size,
        const std::shared_ptr<Message>& msg) {
        const int64_t begin_ts = (int64_t)TimeUtil::GetCurrentTimestamp();
        // panel 链路按 the_file_id 配对 Begin/End(ws_panel_server.cpp),
        // 用逻辑会话、stream、路径、作业 id 和时间戳保证唯一；不同会话可复用同一
        // stream 和作业编号，不能让 Console 将它们收敛为同一个审计事件。
        AuditRecord rec;
        rec.file_id = MD5::Hex(MakeSessionKey(logical_session_id, stream_id)
                               + "#" + path + "#" + std::to_string(job_id)
                               + "#" + std::to_string(begin_ts));
        rec.begin_timestamp = begin_ts;
        rec.logical_session_id = logical_session_id;
        rec.stream_id = stream_id;
        {
            std::lock_guard lock(audits_mutex_);
            audits_[MakeSessionKey(logical_session_id, stream_id) + "#"
                    + std::to_string(job_id)] = rec;
        }

        FileTransferAuditBegin audit;
        audit.file_id = rec.file_id;
        audit.begin_timestamp = begin_ts;
        audit.visitor_device_id = msg->device_id();
        audit.direction = direction;
        audit.file_detail = total_size > 0
            ? path + " (" + std::to_string(total_size) + " bytes)"
            : path;
        if (audit_begin_callback_) {
            audit_begin_callback_(audit);
        }
    }

    void FileTransferService::TrackJobEnd(
        const std::string& logical_session_id,
        const std::string& stream_id, int32_t job_id,
        const std::string& error_or_empty) {
        AuditRecord record;
        {
            std::lock_guard lock(audits_mutex_);
            const auto key = MakeSessionKey(logical_session_id, stream_id)
                             + "#" + std::to_string(job_id);
            const auto found = audits_.find(key);
            if (found == audits_.end()) {
                return;
            }
            record = found->second;
            audits_.erase(found);
        }
        FileTransferAuditEnd audit;
        audit.file_id = record.file_id;
        audit.end_timestamp = (int64_t)TimeUtil::GetCurrentTimestamp();
        audit.duration = audit.end_timestamp - record.begin_timestamp;
        const auto terminal = px::ft::ClassifyTerminal(error_or_empty);
        audit.success = terminal.success;
        audit.status = terminal.status;
        audit.reason = terminal.reason;
        if (audit_end_callback_) {
            audit_end_callback_(audit);
        }
    }

    void FileTransferService::CloseAudits(
        const std::optional<std::string>& logical_session_id,
        const std::string& stream_id, bool success) {
        // 断线/停止时关闭悬挂记录,避免 panel 侧留下只有 Begin 的记录。
        // stream_id 非空时只关该连接的,不影响其他连接在途作业的记录。
        struct AuditId final {
            std::string logical_session_id;
            std::string stream_id;
            int32_t job_id{0};
        };
        std::vector<AuditId> ids;
        {
            std::lock_guard lock(audits_mutex_);
            for (const auto& [key, rec] : audits_) {
                if (!logical_session_id.has_value()
                    || (rec.logical_session_id == *logical_session_id
                        && rec.stream_id == stream_id)) {
                    const auto pos = key.rfind('#');
                    if (pos != std::string::npos) {
                        ids.push_back({rec.logical_session_id, rec.stream_id,
                                       std::stoi(key.substr(pos + 1))});
                    }
                }
            }
        }
        for (const auto& id : ids) {
            TrackJobEnd(id.logical_session_id, id.stream_id, id.job_id,
                        success ? "" : "interrupted");
        }
    }

    FileTransferServiceSnapshot FileTransferService::Snapshot() const {
        FileTransferServiceSnapshot snapshot{
            .running = accepting_.load(),
            .enabled = enabled_.load(),
            .accepted_messages = accepted_messages_.load(),
            .rejected_messages = rejected_messages_.load(),
        };
        {
            std::lock_guard lock(sessions_mutex_);
            snapshot.sessions = sessions_.size();
        }
        {
            std::lock_guard lock(audits_mutex_);
            snapshot.audits = audits_.size();
        }
        return snapshot;
    }

    std::string FileTransferService::MakeSessionKey(
        const std::string& logical_session_id,
        const std::string& stream_id) {
        return logical_session_id + "\x1f" + stream_id;
    }

}  // namespace px::render
