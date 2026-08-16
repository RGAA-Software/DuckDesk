//
// render 被控端文件传输插件(薄壳) — rustdesk 协议迁移阶段 2
//

#include "ft_plugin.h"

#include <filesystem>
#include <vector>

#include "px_message.pb.h"
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

    FtPlugin::~FtPlugin() = default;

    // 对齐 rustdesk check_file_count_limit 的默认上限(DEFAULT_MAX_VALIDATED_FILES,
    // ui_cm_interface.rs)。
    // TODO: 挂到设置体系 —— PxPluginSettingsInfo 暂无此字段,需 panel/SyncConfig
    // 下发链路配合,本阶段先常量。
    static constexpr size_t kMaxTransferFileCount = 10000;

    // rustdesk 调度节拍:有作业 1ms,空闲 30s(MILLI1/SEC30)。
    static constexpr auto kTickBusy = std::chrono::milliseconds(1);
    static constexpr auto kTickIdle = std::chrono::seconds(30);
    // 无作业但刚有活动(如握手消息进了引擎待发队列)时的冲刷宽限期。
    static constexpr auto kActivityGrace = std::chrono::seconds(5);

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

        engine_ = std::make_unique<px::ft::FtEngine>(
            [this](const px::Message& msg) { return this->SendToChannel(msg); });
        engine_->SetLogCallback([](const std::string& msg) {
            LOGW("[ft_engine] {}", msg);
        });
        // 作业终结回调:在 worker 线程(引擎调用线程)触发,审计闭环。
        engine_->SetJobDoneCallback([this](int32_t job_id, int32_t file_num,
                                           const std::string& error_or_empty) {
            (void)file_num;
            this->TrackJobEnd(job_id, error_or_empty);
        });
        // render 无本地 UI,覆盖确认无法问人:延后到队列自动"跳过",避免在
        // HandleDigest 内重入引擎。冲突文件的覆盖/续传决策由主控端 UI 给出
        // (rustdesk 语义里确认请求本就由发起侧 UI 应答)。
        engine_->SetOverwriteConfirmCallback(
            [this](int32_t job_id, int32_t file_num, const std::string& path,
                   bool is_upload, bool is_identical) {
                LOGW("ft overwrite confirm on headless render, auto skip: job {}, file #{}, path {}, upload {}, identical {}",
                     job_id, file_num, path, is_upload, is_identical);
                std::lock_guard<std::mutex> lk(task_mutex_);
                tasks_.emplace_back([this, job_id, file_num]() {
                    if (engine_) engine_->ConfirmFile(job_id, file_num, false, 0);
                });
                task_cv_.notify_one();
            });

        // 限速设置经 OnSyncPluginSettingsInfo 下发(基类不拷贝 max_transmit_speed_
        // 到 sys_settings_,旧插件也是直接从 settings 参数取)。

        last_activity_ = std::chrono::steady_clock::now();
        accepting_ = true;
        worker_ = std::thread([this]() { this->WorkerMain(); });
        LOGI("ft plugin created, worker started.");
        return true;
    }

    bool FtPlugin::OnStop() {
        accepting_ = false;
        StopWorker();
        return PxPluginInterface::OnStop();
    }

    bool FtPlugin::OnDestroy() {
        accepting_ = false;
        // worker 必须先于基类 OnDestroy(清空 net_plugins_)join,见 SendToChannel 注释。
        StopWorker();
        engine_.reset();
        return PxPluginInterface::OnDestroy();
    }

    void FtPlugin::OnMessage(std::shared_ptr<Message> msg) {
        const auto type = msg->type();
        if (type != MessageType::kFileAction && type != MessageType::kFileResponse) {
            return;
        }
        // 权限开关:入口直接拒(旧插件同款语义,rustdesk 回 "No permission of file transfer")。
        if (!sys_settings_.file_transfer_enabled_) {
            if (type == MessageType::kFileAction) {
                ReplyNoPermission(msg);
            }
            return;
        }
        // 分发线程只入队,不做任何磁盘 IO(慢插件告警:px_plugin_interface.cpp:268)。
        if (!accepting_.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, m = std::move(msg)]() { this->ProcessMessage(m); });
        }
        task_cv_.notify_one();
    }

    void FtPlugin::OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id) {
        if (!accepting_.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this]() {
                // 断线清理:保留 .download/.digest 供续传(区别于显式取消)。
                if (engine_) engine_->DisconnectCleanup();
                CloseAllAudits(false);
            });
        }
        task_cv_.notify_one();
    }

    void FtPlugin::OnSyncPluginSettingsInfo(const PxPluginSettingsInfo& settings) {
        PxPluginInterface::OnSyncPluginSettingsInfo(settings);
        if (!accepting_.load()) {
            return;
        }
        const uint64_t rate_bps = settings.max_transmit_speed_ / 8;
        std::lock_guard<std::mutex> lk(task_mutex_);
        tasks_.emplace_back([this, rate_bps]() {
            if (engine_) engine_->SetRateLimitBytesPerSec(rate_bps);
        });
        task_cv_.notify_one();
    }

    // ---------------- worker 线程 ----------------

    void FtPlugin::WorkerMain() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(task_mutex_);
                // 引擎状态仅本线程持有,这里读安全。
                const bool has_jobs = engine_ &&
                    (!engine_->read_jobs().empty() || !engine_->write_jobs().empty());
                const bool recent_activity =
                    std::chrono::steady_clock::now() - last_activity_ < kActivityGrace;
                const auto timeout = (has_jobs || recent_activity) ? kTickBusy : kTickIdle;
                task_cv_.wait_for(lk, timeout, [this]() {
                    return !tasks_.empty() || worker_exit_;
                });
                if (worker_exit_ && tasks_.empty()) {
                    break;
                }
                if (!tasks_.empty()) {
                    task = std::move(tasks_.front());
                    tasks_.pop_front();
                }
            }
            if (task) {
                task();
            }
            if (engine_) {
                engine_->Tick();
            }
        }
        LOGI("ft plugin worker exited.");
    }

    void FtPlugin::StopWorker() {
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            if (worker_exit_) {
                // 已经发起过停止,只需 join。
            } else {
                // 在途作业按取消语义处理(清 .download/.digest,并通知对端)。
                tasks_.emplace_back([this]() {
                    if (engine_) {
                        std::vector<int32_t> ids;
                        for (const auto& job : engine_->read_jobs()) ids.push_back(job.id());
                        for (const auto& job : engine_->write_jobs()) ids.push_back(job.id());
                        for (int32_t id : ids) engine_->CancelJob(id);
                    }
                    CloseAllAudits(false);
                });
                worker_exit_ = true;
            }
        }
        task_cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
    }

    void FtPlugin::ProcessMessage(const std::shared_ptr<Message>& msg) {
        if (!engine_) {
            return;
        }
        if (msg->type() == MessageType::kFileAction) {
            const auto& action = msg->file_action();
            using U = px::FileAction::UnionCase;
            if (!CheckReadPathExists(action) || !CheckFileCountLimit(action)) {
                return;
            }
            // 审计:传输作业开始(目录操作不建作业,不入审计)。
            if (action.union_case() == U::kSend) {
                // 对端请求下载:render -> 主控
                TrackJobBegin(action.send().id(), "Out", action.send().path(), 0, msg);
            } else if (action.union_case() == U::kReceive) {
                // 对端上传:主控 -> render
                TrackJobBegin(action.receive().id(), "In", action.receive().path(),
                              action.receive().total_size(), msg);
            }
            current_stream_id_ = msg->stream_id();
            engine_->HandleFileAction(action);
        } else {
            current_stream_id_ = msg->stream_id();
            engine_->HandleFileResponse(msg->file_response());
        }
        last_activity_ = std::chrono::steady_clock::now();
    }

    bool FtPlugin::SendToChannel(const px::Message& msg) {
        // 线程安全性:见头文件注释(可从 worker 直调,无需回程队列)。
        // 反压:通道忙时返回 false,引擎压入待发队列、本 tick 不再读盘。
        if (GetQueuingFtMsgCountInNetPlugins() > kFtQueueBusyThreshold) {
            return false;
        }
        for (const auto& [id, np] : net_plugins_) {
            if (np->GetConnectedClientsCount() > 0 && !np->HasEnoughBufferForQueuingFtMessages()) {
                return false;
            }
        }
        // 引擎不感知通道,type/stream_id/device_id 由壳补齐。
        px::Message out = msg;
        if (out.has_file_response()) {
            out.set_type(MessageType::kFileResponse);
        } else if (out.has_file_action()) {
            out.set_type(MessageType::kFileAction);
        }
        out.set_stream_id(current_stream_id_);
        out.set_device_id(sys_settings_.device_id_);
        DispatchTargetFileTransferMessage(current_stream_id_, ProtoAsData(&out));
        return true;
    }

    // ---------------- 权限 / 上限 ----------------

    void FtPlugin::ReplyNoPermission(const std::shared_ptr<Message>& in_msg) {
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
        DispatchTargetFileTransferMessage(in_msg->stream_id(), ProtoAsData(&out));
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

    bool FtPlugin::CheckFileCountLimit(const px::FileAction& action) {
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
        SendToChannel(px::ft::NewError(id, "Too many files", file_num));
        return false;
    }

    bool FtPlugin::CheckReadPathExists(const px::FileAction& action) {
        using U = px::FileAction::UnionCase;
        if (action.union_case() != U::kSend) {
            return true;
        }
        std::error_code ec;
        if (std::filesystem::exists(px::ft::ToFsPath(action.send().path()), ec)) {
            return true;
        }
        LOGW("ft rejected: path not exists: {}", action.send().path());
        SendToChannel(px::ft::NewError(action.send().id(), "Path not exists",
                                       action.send().file_num()));
        return false;
    }

    // ---------------- 审计 ----------------

    void FtPlugin::TrackJobBegin(int32_t job_id, const std::string& direction,
                                 const std::string& path, uint64_t total_size,
                                 const std::shared_ptr<Message>& msg) {
        const int64_t begin_ts = (int64_t)TimeUtil::GetCurrentTimestamp();
        // panel 链路按 the_file_id 配对 Begin/End(ws_panel_server.cpp),
        // 用 路径+作业id+时间戳 保证唯一(剪贴板旧实现只 hash 路径,重复传输会撞)。
        AuditRecord rec;
        rec.the_file_id_ = MD5::Hex(path + "#" + std::to_string(job_id) + "#" + std::to_string(begin_ts));
        rec.begin_timestamp_ = begin_ts;
        audits_[job_id] = rec;

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

    void FtPlugin::TrackJobEnd(int32_t job_id, const std::string& error_or_empty) {
        auto it = audits_.find(job_id);
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

    void FtPlugin::CloseAllAudits(bool success) {
        // 断线/停止时关闭所有悬挂记录,避免 panel 侧留下只有 Begin 的记录。
        std::vector<int32_t> ids;
        for (const auto& [id, _] : audits_) {
            ids.push_back(id);
        }
        for (int32_t id : ids) {
            TrackJobEnd(id, success ? "" : "interrupted");
        }
    }

}
