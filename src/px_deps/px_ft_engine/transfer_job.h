// px_ft_engine - TransferJob 传输作业引擎
// 对照 rustdesk/libs/hbb_common/src/fs.rs 的 TransferJob 逐函数移植(同步 C++ 版)。
// 异步 tokio 语义 -> 调用方 worker 线程上的同步调用;网络发送经注入回调完成。
//
// 有意偏离点(与 fs.rs 对照):
// 1. BUF_SIZE 128KB(fs.rs:953)-> kBlockPayloadSize 120KB:render 侧 TLV 分片阈值
//    128KB,块载荷定为 120KB 避免每块恰在分片边界(migration plan §5.4)。
// 2. FileTransferSendConfirmRequest.offset_blk 沿上游命名,但实际是**字节偏移**
//    (migration plan §5.1),代码中以 offset_bytes 命名变量。
// 3. 压缩算法 zstd -> miniz deflate(见 ft_compress.h)。
// 4. JobType::Printer(打印作业)保留了字段与分支,但上层不启用。
#pragma once

#include <cstdint>
#include <fstream>
#include <functional>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "ft_path.h"
#include "px_file_transfer.pb.h"
#include "px_message.pb.h"

namespace px::ft {

// 每块载荷字节数(fs.rs:953 BUF_SIZE=128KB,此处定 120KB 避免 TLV 边界分片)
inline constexpr size_t kBlockPayloadSize = 120 * 1024;

// fs.rs:262 JobType
enum class JobType : int32_t { Generic = 0, Printer = 1 };

// fs.rs:382 FileDigest(.digest 凭证内容)
struct FileDigest {
    uint64_t size = 0;
    uint64_t modified = 0;
};

// fs.rs:310 DataSource::MemoryCursor
struct MemoryCursor {
    std::vector<uint8_t> data;
    size_t read_pos = 0;
};

// fs.rs:310 DataSource:文件路径 或 内存流
using DataSource = std::variant<std::filesystem::path, MemoryCursor>;

// fs.rs:351 DataStream:文件流 或 内存流
class DataStream {
public:
    DataStream() = default;
    DataStream(DataStream&&) noexcept = default;
    DataStream& operator=(DataStream&&) noexcept = default;
    DataStream(const DataStream&) = delete;
    DataStream& operator=(const DataStream&) = delete;

    // 打开失败抛 std::runtime_error
    static DataStream OpenForRead(const std::filesystem::path& p);
    static DataStream CreateForWrite(const std::filesystem::path& p); // truncate 新建
    // 续传用:不截断打开(不存在则新建),配合 SeekStart 定位写入点
    static DataStream OpenForWriteNoTrunc(const std::filesystem::path& p);
    // 内存流:接管 cursor 数据(fs.rs:872 std::mem::swap 语义)
    static DataStream FromMemory(MemoryCursor&& cursor);

    bool IsFile() const { return std::holds_alternative<std::fstream>(stream_); }

    void WriteAll(const uint8_t* data, size_t len); // 失败抛异常
    size_t ReadSome(uint8_t* buf, size_t len);      // 返回实际读取数,0 = EOF
    bool SeekStart(uint64_t offset);                // 文件流定位(读或写游标)
    void SyncAll();                                 // 落盘(fsync 语义)

private:
    explicit DataStream(std::fstream&& fs) : stream_(std::move(fs)) {}
    explicit DataStream(MemoryCursor&& c) : stream_(std::move(c)) {}

    std::variant<std::fstream, MemoryCursor> stream_;
};

// 发送回调:引擎产出的一切 px::Message 经此发出。
// 返回 false 表示通道忙(发送水位满),消息未被接受 —— 引擎不得继续读盘积块。
using SendFunc = std::function<bool(const px::Message&)>;

// fs.rs:388 TransferJob
class TransferJob {
public:
    TransferJob() = default;
    TransferJob(TransferJob&&) noexcept = default;
    TransferJob& operator=(TransferJob&&) noexcept = default;
    TransferJob(const TransferJob&) = delete;
    TransferJob& operator=(const TransferJob&) = delete;

    // fs.rs:565 new_write
    static TransferJob NewWrite(int32_t id, JobType type, std::string remote, DataSource data_source,
                                int32_t file_num, bool show_hidden, bool is_remote,
                                bool enable_overwrite_detection);
    // fs.rs:596 new_read:FilePath 源会递归展开文件列表(get_recursive_files),失败抛异常
    static TransferJob NewRead(int32_t id, JobType type, std::string remote, DataSource data_source,
                               int32_t file_num, bool show_hidden, bool is_remote,
                               bool enable_overwrite_detection);

    // fs.rs:646 set_files:整体校验(validate_transfer_file_names + 符号链接组件)并累加 total_size
    void SetFiles(std::vector<px::FileEntry> files);

    // ---------------- 读侧(发送方) ----------------

    // fs.rs:893 init_data_stream:打开当前文件流;
    // 开启覆盖检测时经 send 发出 FileTransferDigest 并置等待。
    // send 返回 false(通道忙)时不置等待位,下个 tick 重试。
    // 返回 false 表示作业已无更多文件。
    bool InitDataStream(const SendFunc& send);

    // fs.rs:929 read:读下一块(≤kBlockPayloadSize),必要时压缩。
    // 返回 nullopt:当前文件读完/等待覆盖确认/内存流耗尽。
    std::optional<px::FileTransferBlock> Read();

    // fs.rs:1156 confirm:收到对端 skip / offset_blk(字节偏移)确认
    bool Confirm(const px::FileTransferSendConfirmRequest& req);

    // fs.rs:1105 set_stream_offset:按字节偏移定位流(续传)。
    // 优先 .download+.digest 都在 -> 打开 .download 写流定位;否则正式文件存在 -> 打开读流定位。
    void SetStreamOffset(int32_t file_num, uint64_t offset_bytes);

    // ---------------- 写侧(接收方) ----------------

    // fs.rs:760 write:落 <path>.download,维护 <path>.digest 凭证 JSON({size, modified})
    void Write(const px::FileTransferBlock& block);

    // fs.rs:704 modify_time:rename .download -> 正式文件、删 digest、恢复 mtime。
    // rename 失败(如目标被独占占用)时保留 .download/.digest 供续传并抛
    // std::runtime_error,调用方负责让作业以错误终结(有意偏离上游 .ok() 静默)。
    void ModifyTime();

    // fs.rs:728 remove_download_file:显式取消时清除 .download/.digest(断线不调,保留续传)
    void RemoveDownloadFile();

    // fs.rs:748 set_finished_size_on_resume
    void SetFinishedSizeOnResume();

    // ---------------- 状态 ----------------

    int32_t id() const { return id_; }
    JobType type() const { return type_; }
    const std::string& remote() const { return remote_; }
    // 归属连接标识(插件壳传入,通常是访客 stream_id);断线按连接清理用,
    // 不参与任何协议语义。空 = 未标记(旧调用方/单连接场景)。
    const std::string& conn_id() const { return conn_id_; }
    void set_conn_id(std::string v) { conn_id_ = std::move(v); }
    const DataSource& data_source() const { return data_source_; }
    const std::vector<px::FileEntry>& files() const { return files_; }
    int32_t file_num() const { return file_num_; }
    void set_file_num(int32_t n) { file_num_ = n; }
    uint64_t total_size() const { return total_size_; }
    void set_total_size(uint64_t v) { total_size_ = v; }
    uint64_t finished_size() const { return finished_size_; }
    uint64_t transferred() const { return transferred_; }

    bool file_confirmed() const { return file_confirmed_; }
    void set_file_confirmed(bool v);
    bool file_is_waiting() const { return file_is_waiting_; }
    void set_file_is_waiting(bool v) { file_is_waiting_ = v; }
    bool file_skipped() const { return file_skipped_; }
    // fs.rs:1095 set_file_skipped(恒返回 true,与上游一致)
    bool set_file_skipped();
    // fs.rs:1070 job_skipped:整个任务被跳过(单文件且被跳过)
    bool job_skipped() const { return file_skipped_ && files_.size() == 1; }
    // fs.rs:1082 job_completed:read 返回 nullopt 后是否可自动删除作业
    bool job_completed() const {
        return !enable_overwrite_detection_ || (!file_confirmed_ && !file_is_waiting_);
    }
    // fs.rs:1088 job_error
    std::optional<std::string> job_error() const {
        if (job_skipped()) return std::string("skipped");
        return std::nullopt;
    }

    void set_digest(uint64_t size, uint64_t modified) { digest_ = {size, modified}; }
    const FileDigest& digest() const { return digest_; }

    void set_overwrite_strategy(std::optional<bool> v) { default_overwrite_strategy_ = v; }
    std::optional<bool> default_overwrite_strategy() const { return default_overwrite_strategy_; }

    bool show_hidden = false;
    bool is_remote = false;
    bool is_last_job = false;
    bool is_resume = false;

    // fs.rs:831 join
    static std::filesystem::path Join(const std::filesystem::path& p, const std::string& name) {
        if (name.empty()) return p;
        return p / ToFsPath(name);
    }

private:
    // fs.rs:842 open_data_stream;返回 true 表示无更多文件(done)
    bool OpenDataStream();
    // fs.rs:881 get_current_digest:当前打开文件的 (last_modified, file_size)
    std::pair<uint64_t, uint64_t> GetCurrentDigest() const;
    // fs.rs:1011 send_current_digest
    bool SendCurrentDigest(const SendFunc& send);
    // fs.rs:690 resolve_entry_path:Generic 走校验拼接,Printer 直接拼接;非法返回 nullopt
    std::optional<std::filesystem::path> ResolveEntryPath(const std::filesystem::path& base,
                                                          const std::string& name) const;

    int32_t id_ = 0;
    JobType type_ = JobType::Generic;
    std::string remote_;
    std::string conn_id_;
    DataSource data_source_;
    int32_t file_num_ = 0;
    std::vector<px::FileEntry> files_;

    std::optional<DataStream> data_stream_;
    std::filesystem::path current_file_path_; // 读侧当前打开文件的完整路径(digest 用)
    uint64_t total_size_ = 0;
    uint64_t finished_size_ = 0;
    uint64_t transferred_ = 0;
    bool enable_overwrite_detection_ = false;
    bool file_confirmed_ = false;
    bool file_skipped_ = false;
    bool file_is_waiting_ = false;
    std::optional<bool> default_overwrite_strategy_;
    FileDigest digest_;
};

// ---------------- 作业表操作(fs.rs:1306-1319) ----------------

std::optional<TransferJob> RemoveJob(int32_t id, std::vector<TransferJob>& jobs);
TransferJob* GetJob(int32_t id, std::vector<TransferJob>& jobs);
const TransferJob* GetJob(int32_t id, const std::vector<TransferJob>& jobs);

// ---------------- Digest 覆盖决策(fs.rs:1442-1510) ----------------

struct DigestCheckResult {
    enum class Kind { IsSame, NeedConfirm, NoSuchFile };
    Kind kind;
    px::FileTransferDigest digest; // kind == NeedConfirm 时有效
};

// fs.rs:1449 is_write_need_confirmation:
// - is_resume 且 <path>.digest 与 <path>.download 都存在:比对凭证,identical 且已传 >0
//   -> NeedConfirm(transferred_size=已收字节数,续传)
// - 正式文件存在:比对 mtime+size -> NeedConfirm(is_identical)
// - 都不存在 -> NoSuchFile
DigestCheckResult IsWriteNeedConfirmation(bool is_resume, const std::string& file_path,
                                          const px::FileTransferDigest& digest);

// ---------------- 消息构造(fs.rs:1200-1303) ----------------

px::Message NewError(int32_t id, const std::string& err, int32_t file_num);
px::Message NewDir(int32_t id, std::string path, std::vector<px::FileEntry> files);
px::Message NewBlock(px::FileTransferBlock block);
px::Message NewSendConfirm(px::FileTransferSendConfirmRequest req);
px::Message NewReceive(int32_t id, std::string path, int32_t file_num,
                       std::vector<px::FileEntry> files, uint64_t total_size);
px::Message NewSend(int32_t id, JobType type, std::string path, int32_t file_num,
                    bool include_hidden);
px::Message NewDone(int32_t id, int32_t file_num);
px::Message NewCancel(int32_t id);

} // namespace px::ft
