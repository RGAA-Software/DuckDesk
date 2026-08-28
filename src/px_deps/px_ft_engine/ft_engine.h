// px_ft_engine - 文件传输引擎外壳
// 持有一对 read_jobs / write_jobs(对应 rustdesk io_loop.rs 双队列),
// 负责:作业调度(handle_read_jobs,fs.rs:1336)、Digest 覆盖确认决策(io_loop.rs:1570)、
// 目录操作分发(connection.rs:3403)、限速钩子与进度上报。
//
// ============================ 线程假设 ============================
// 全部公有方法(含 Tick)必须在同一线程(插件的 worker 线程)上调用,
// 引擎内部不做任何线程同步。所有磁盘 IO 都发生在这些调用内。
// 插件侧负责:分发线程只把解析好的 px::Message move 进队列,worker 线程取出后
// 调 HandleFileAction/HandleFileResponse;定时器(建议 1~10ms)驱动 Tick()。
//
// ============================ 反压语义 ============================
// 引擎不直接发网络消息,一切经 SendFunc 回调(对应 rustdesk 的 stream.send)。
// 回调返回 false 表示"通道忙"(发送水位满):消息进入引擎内的小容量待发队列,
// 当前 tick 不再读盘、不积压新块;下个 tick 先尝试冲刷待发队列,
// 冲刷不动则整个 tick 跳过(对齐 migration plan §2 "忙时不读盘不积块")。
//
// ============================ 限速钩子 ============================
// SetRateLimitBytesPerSec 设置令牌桶限速(字节/秒),0 = 不限速(默认)。
// 仅限制数据块吞吐,握手小消息不受限。沿用旧协议 max_transmit_speed 的设置语义。
//
// ============================ 进度上报 ============================
// SetProgressCallback 后,约每秒一次回调各作业进度快照,
// 速度用 1s 差值法计算(io_loop.rs:1048 update_jobs_status 语义):
// speed = (transferred 差值) / (间隔秒数)。
#pragma once

#include <chrono>
#include <deque>
#include <memory>
#include <unordered_map>

#include "transfer_job.h"

namespace px::ft {

// 进度快照(对应 io_loop.rs job_progress 上报内容)
struct TransferJobStatus {
    int32_t id = 0;
    int32_t file_num = 0;      // 当前文件序号
    int32_t file_count = 0;    // 文件总数
    uint64_t total_size = 0;
    uint64_t finished_size = 0;
    uint64_t transferred = 0;  // 线上字节数(压缩后)
    double speed = 0.0;        // 字节/秒,1s 差值法
    bool is_remote = false;    // true: 从远端下载; false: 上传到远端
    bool done = false;
    bool cancel = false;
    std::string error;
};

struct PreparedOutboundMessage {
    std::uint64_t token = 0;
    std::shared_ptr<const px::Message> message;
};

class FtEngine {
public:
    // 发送回调:见文件头"反压语义"。
    using SendFunc = px::ft::SendFunc;
    // 覆盖确认请求(io_loop.rs override_file_confirm 语义):
    // 主控侧收到 Digest 且无法自动决策(无 default_overwrite_strategy)时回调,
    // 上层(UI)决策后调 ConfirmFile() 回喂结果。
    // path 为发生冲突的本/对端文件路径;is_upload=true 表示上传方向
    // (被控回发的 digest,我方是读侧)。
    // 注意:被控侧(写作业 is_remote=false)的 NeedConfirm 不回调此接口,
    // 引擎直接回发 is_upload=true 的 digest 给主控(ui_cm_interface.rs:1116)。
    using OverwriteConfirmFunc =
        std::function<void(int32_t job_id, int32_t file_num, const std::string& path,
                           bool is_upload, bool is_identical)>;
    // 进度回调:每秒一次,逐作业回调 TransferJobStatus。
    using ProgressFunc = std::function<void(const TransferJobStatus&)>;
    // 作业终结回调:done / error / cancel 时触发一次。
    using JobDoneFunc =
        std::function<void(int32_t job_id, int32_t file_num, const std::string& error_or_empty)>;
    // 数据类响应透传(read_dir / all_files / read_empty_dirs 的结果,引擎不消费)。
    using ResponseFunc = std::function<void(const px::FileResponse&)>;
    // 日志回调(默认 stderr)。
    using LogFunc = std::function<void(const std::string&)>;

    // Two-phase mode. The owner drains PrepareOutbound() and must call exactly
    // one of CommitOutbound()/RetryOutbound() for the returned token.
    FtEngine();
    // Compatibility mode for callers not migrated to the two-phase contract.
    explicit FtEngine(SendFunc send);

    void SetOverwriteConfirmCallback(OverwriteConfirmFunc cb) { overwrite_confirm_cb_ = std::move(cb); }
    void SetProgressCallback(ProgressFunc cb) { progress_cb_ = std::move(cb); }
    void SetJobDoneCallback(JobDoneFunc cb) { job_done_cb_ = std::move(cb); }
    void SetResponseCallback(ResponseFunc cb) { response_cb_ = std::move(cb); }
    void SetLogCallback(LogFunc cb) { log_cb_ = std::move(cb); }

    // 限速:字节/秒;0 = 不限速(默认)
    void SetRateLimitBytesPerSec(uint64_t bps);

    // timer 驱动:每 tick 只推进一个非等待读作业一块(fs.rs:1336/1376 break 语义)。
    // 同时负责:待发队列冲刷、读作业初始化(发 Digest)、限速令牌补充、每秒进度回调。
    void Tick();

    [[nodiscard]] std::optional<PreparedOutboundMessage> PrepareOutbound() const;
    [[nodiscard]] bool CommitOutbound(std::uint64_t token);
    [[nodiscard]] bool RetryOutbound(std::uint64_t token) const;
    [[nodiscard]] bool HasPendingOutbound() const { return !outbox_.empty(); }
    [[nodiscard]] std::size_t PendingOutboundCount() const { return outbox_.size(); }

    // ---------------- 对端消息入口 ----------------
    // conn_id: 该消息归属的连接标识(插件壳传 px::Message.stream_id),
    // 新建作业会带上它,供 DisconnectCleanup(conn_id) 按连接清理。
    void HandleFileAction(const px::FileAction& action, const std::string& conn_id = "");
    void HandleFileResponse(const px::FileResponse& resp);

    // ---------------- 本端主动操作 ----------------

    // 上传(io_loop.rs Data::SendFiles is_remote=false):
    // 本地建读作业(递归展开 path),向对端发 FileTransferReceiveRequest。返回作业 id。
    // is_resume=true 标记续传(io_loop.rs ResumeJob 语义):digest 携带 is_resume,
    // 对端写侧据此消费 .digest 凭证。
    int32_t SendFiles(const std::string& local_path, bool include_hidden,
                      const std::string& remote_to, int32_t file_num = 0, bool is_resume = false,
                      const std::string& conn_id = "");

    // 下载(io_loop.rs Data::SendFiles is_remote=true):
    // 本地建写作业写到 local_to,向对端发 FileTransferSendRequest。返回作业 id。
    // is_resume=true 时本地写作业标记续传,IsWriteNeedConfirmation 消费本地 .digest 凭证。
    int32_t ReceiveFiles(const std::string& remote_path, bool include_hidden,
                         const std::string& local_to, int32_t file_num = 0,
                         bool is_resume = false, const std::string& conn_id = "");

    void ReadDir(const std::string& path, bool include_hidden);
    void ReadAllFiles(int32_t id, const std::string& path, bool include_hidden);
    void ReadEmptyDirs(const std::string& path, bool include_hidden);
    void CreateDir(int32_t id, const std::string& path);
    void RemoveDir(int32_t id, const std::string& path, bool recursive);
    void RemoveFile(int32_t id, const std::string& path, int32_t file_num = 0);
    void RenameFile(int32_t id, const std::string& path, const std::string& new_name);

    // 显式取消:本地读/写作业都移除;写作业清 .download/.digest(remove_download_file),
    // 并向对端发 FileTransferCancel。断线场景不要调这个,用 DisconnectCleanup()。
    void CancelJob(int32_t id);
    // 断线清理:移除该连接的作业,保留 .download/.digest 供续传。
    // conn_id 非空时只清该连接的作业(迟到的断线事件不会误杀其他/新会话作业);
    // conn_id 为空 = 清全部(单连接/旧调用方语义),同时清空待发队列。
    void DisconnectCleanup(const std::string& conn_id = "");

    // "应用到全部"覆盖策略(io_loop.rs:673 job.default_overwrite_strategy)
    void SetOverwriteStrategy(int32_t id, std::optional<bool> overwrite);

    // UI 覆盖决策回喂:overwrite=true 覆盖(可带续传偏移,单位字节),false 跳过。
    void ConfirmFile(int32_t id, int32_t file_num, bool overwrite, uint64_t offset_bytes = 0);

    // 作业表访问(插件层排队/恢复场景直接操作,对齐 io_loop.rs 直推 read_jobs/write_jobs)
    void AddReadJob(TransferJob job) { read_jobs_.push_back(std::move(job)); }
    void AddWriteJob(TransferJob job) { write_jobs_.push_back(std::move(job)); }
    const std::vector<TransferJob>& read_jobs() const { return read_jobs_; }
    const std::vector<TransferJob>& write_jobs() const { return write_jobs_; }

    // 引擎侧作业 id 分配(fs.rs:25 NEXT_JOB_ID)
    static int32_t NextJobId();

private:
    void Log(const std::string& msg);
    // 经反压队列发送:返回 false 表示进了待发队列(通道忙)
    bool Send(const px::Message& msg);
    // tick 开头冲刷待发队列;返回 false 表示仍冲不动(本 tick 不再读盘)
    bool FlushOutbox();

    void HandleDigest(const px::FileTransferDigest& digest);
    void HandleBlock(const px::FileTransferBlock& block);
    void HandleDone(const px::FileTransferDone& done);
    void UpdateJobsStatus();

    struct OutboundEntry {
        std::uint64_t token = 0;
        std::shared_ptr<const px::Message> message;
    };

    SendFunc legacy_send_;
    OverwriteConfirmFunc overwrite_confirm_cb_;
    ProgressFunc progress_cb_;
    JobDoneFunc job_done_cb_;
    ResponseFunc response_cb_;
    LogFunc log_cb_;

    std::vector<TransferJob> read_jobs_;
    std::vector<TransferJob> write_jobs_;

    // 反压待发队列(小容量:每 tick 至多 1 块 + 少量握手消息)
    std::deque<OutboundEntry> outbox_;
    std::uint64_t next_outbound_token_ = 1;

    // 令牌桶限速
    uint64_t rate_bps_ = 0; // 0 = 不限速
    double bucket_tokens_ = 0;
    std::chrono::steady_clock::time_point last_refill_;

    // 进度上报(1s 差值法)
    std::chrono::steady_clock::time_point last_status_time_;
    std::unordered_map<int32_t, uint64_t> last_transferred_;
};

} // namespace px::ft
