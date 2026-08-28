#include "transfer_job.h"

#include <cstring>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "ft_compress.h"

namespace px::ft {

namespace {

[[noreturn]] void Bail(const std::string& msg) { throw std::runtime_error(msg); }

// .digest 凭证 JSON(fs.rs:760 std::fs::write(dp, json!(self.digest).to_string()))
// 字段名与 serde 序列化的 FileDigest 一致:{"size":N,"modified":M}
void WriteDigestFile(const std::string& digest_path, const FileDigest& digest) {
    nlohmann::json j;
    j["size"] = digest.size;
    j["modified"] = digest.modified;
    std::ofstream ofs(ToFsPath(digest_path), std::ios::binary | std::ios::trunc);
    if (ofs) ofs << j.dump();
}

std::optional<FileDigest> ReadDigestFile(const std::string& digest_path) {
    std::ifstream ifs(ToFsPath(digest_path), std::ios::binary);
    if (!ifs) return std::nullopt;
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    nlohmann::json j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return std::nullopt;
    if (!j.contains("size") || !j.contains("modified")) return std::nullopt;
    return FileDigest{j["size"].get<uint64_t>(), j["modified"].get<uint64_t>()};
}

} // namespace

// ---------------- DataStream ----------------

DataStream DataStream::OpenForRead(const std::filesystem::path& p) {
    std::fstream fs(p, std::ios::in | std::ios::binary);
    if (!fs) Bail("failed to open file for read: " + ToUtf8(p));
    return DataStream(std::move(fs));
}

DataStream DataStream::CreateForWrite(const std::filesystem::path& p) {
    std::fstream fs(p, std::ios::out | std::ios::binary | std::ios::trunc);
    if (!fs) Bail("failed to create file for write: " + ToUtf8(p));
    return DataStream(std::move(fs));
}

DataStream DataStream::OpenForWriteNoTrunc(const std::filesystem::path& p) {
    std::error_code ec;
    if (!std::filesystem::exists(p, ec)) {
        return CreateForWrite(p);
    }
    // ios::in | ios::out:不截断,可读写定位
    std::fstream fs(p, std::ios::in | std::ios::out | std::ios::binary);
    if (!fs) Bail("failed to open file for write: " + ToUtf8(p));
    return DataStream(std::move(fs));
}

DataStream DataStream::FromMemory(MemoryCursor&& cursor) {
    return DataStream(std::move(cursor));
}

void DataStream::WriteAll(std::span<const uint8_t> data) {
    if (std::holds_alternative<std::fstream>(stream_)) {
        auto& fs = std::get<std::fstream>(stream_);
        fs.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
        if (!fs) Bail("file write failed");
    } else {
        auto& c = std::get<MemoryCursor>(stream_);
        c.data.insert(c.data.end(), data.begin(), data.end());
    }
}

size_t DataStream::ReadSome(std::span<uint8_t> buffer) {
    if (std::holds_alternative<std::fstream>(stream_)) {
        auto& fs = std::get<std::fstream>(stream_);
        fs.read(reinterpret_cast<char*>(buffer.data()),
                static_cast<std::streamsize>(buffer.size()));
        return static_cast<size_t>(fs.gcount());
    }
    auto& c = std::get<MemoryCursor>(stream_);
    size_t remain = c.data.size() - c.read_pos;
    size_t n = remain < buffer.size() ? remain : buffer.size();
    if (n > 0) {
        std::copy_n(c.data.begin() + c.read_pos, n, buffer.begin());
        c.read_pos += n;
    }
    return n;
}

bool DataStream::SeekStart(uint64_t offset) {
    if (std::holds_alternative<std::fstream>(stream_)) {
        auto& fs = std::get<std::fstream>(stream_);
        fs.clear();
        fs.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        fs.seekp(static_cast<std::streamoff>(offset), std::ios::beg);
        return !fs.fail();
    }
    auto& c = std::get<MemoryCursor>(stream_);
    if (offset > c.data.size()) return false;
    c.read_pos = static_cast<size_t>(offset);
    return true;
}

void DataStream::SyncAll() {
    if (std::holds_alternative<std::fstream>(stream_)) {
        std::get<std::fstream>(stream_).flush();
    }
}

// ---------------- TransferJob 构造 ----------------

TransferJob TransferJob::NewWrite(int32_t id, JobType type, std::string remote,
                                  DataSource data_source, int32_t file_num, bool show_hidden,
                                  bool is_remote, bool enable_overwrite_detection) {
    TransferJob job;
    job.id_ = id;
    job.type_ = type;
    job.remote_ = std::move(remote);
    job.data_source_ = std::move(data_source);
    job.file_num_ = file_num;
    job.show_hidden = show_hidden;
    job.is_remote = is_remote;
    job.enable_overwrite_detection_ = enable_overwrite_detection;
    return job;
}

TransferJob TransferJob::NewRead(int32_t id, JobType type, std::string remote,
                                 DataSource data_source, int32_t file_num, bool show_hidden,
                                 bool is_remote, bool enable_overwrite_detection) {
    TransferJob job = NewWrite(id, type, std::move(remote), std::move(data_source), file_num,
                               show_hidden, is_remote, enable_overwrite_detection);
    // fs.rs:607 - FilePath 递归展开;MemoryCursor 无文件列表,total_size = 数据长度
    if (std::holds_alternative<std::filesystem::path>(job.data_source_)) {
        const auto& path = std::get<std::filesystem::path>(job.data_source_);
        job.files_ = GetRecursiveFiles(ToUtf8(path), show_hidden);
        uint64_t total = 0;
        for (const auto& f : job.files_) total += f.size();
        job.total_size_ = total;
    } else {
        job.total_size_ = std::get<MemoryCursor>(job.data_source_).data.size();
    }
    return job;
}

void TransferJob::SetFiles(std::vector<px::FileEntry> files) {
    // fs.rs:646 set_files
    ValidateTransferFileNames(files);
    if (std::holds_alternative<std::filesystem::path>(data_source_)) {
        const auto& base = std::get<std::filesystem::path>(data_source_);
        for (const auto& file : files) {
            ValidateNoSymlinkComponents(base, file.name());
        }
    }
    uint64_t total = 0;
    for (const auto& f : files) total += f.size();
    total_size_ = total;
    files_ = std::move(files);
}

// ---------------- 读侧 ----------------

bool TransferJob::OpenDataStream() {
    // fs.rs:842
    size_t file_num = static_cast<size_t>(file_num_);
    if (std::holds_alternative<std::filesystem::path>(data_source_)) {
        const auto& path = std::get<std::filesystem::path>(data_source_);
        if (file_num >= files_.size()) {
            data_stream_.reset();
            return true; // job done
        }
        if (!data_stream_) {
            std::filesystem::path file_path = Join(path, files_[file_num].name());
            try {
                data_stream_ = DataStream::OpenForRead(file_path);
            } catch (...) {
                // fs.rs:861 - 打开失败与校验失败同处理:推进到下一文件并抛错
                file_num_ += 1;
                file_confirmed_ = false;
                file_is_waiting_ = false;
                throw;
            }
            current_file_path_ = std::move(file_path);
            read_hasher_.emplace();
            next_read_block_id_ = 1;
            file_confirmed_ = false;
            file_is_waiting_ = false;
        }
    } else {
        if (!data_stream_) {
            // fs.rs:871 - swap 语义:接管 cursor 数据
            auto& cursor = std::get<MemoryCursor>(data_source_);
            MemoryCursor owned;
            std::swap(owned, cursor);
            data_stream_ = DataStream::FromMemory(std::move(owned));
        }
    }
    return false;
}

std::pair<uint64_t, uint64_t> TransferJob::GetCurrentDigest() const {
    // fs.rs:881 - BufStream 无 digest
    if (!data_stream_ || !data_stream_->IsFile()) Bail("No digest for buf stream");
    std::error_code ec;
    uint64_t size = std::filesystem::file_size(current_file_path_, ec);
    if (ec) Bail(ec.message());
    uint64_t last_modified = GetFileMtimeSecs(current_file_path_);
    return {last_modified, size};
}

bool TransferJob::SendCurrentDigest(const SendFunc& send) {
    // fs.rs:1011
    auto [last_modified, file_size] = GetCurrentDigest();
    px::Message msg;
    auto& digest = *msg.mutable_file_response()->mutable_digest();
    digest.set_id(id_);
    digest.set_file_num(file_num_);
    digest.set_last_modified(last_modified);
    digest.set_file_size(file_size);
    digest.set_is_resume(is_resume);
    digest.set_capabilities(kFtCurrentCapabilities);
    return send(msg);
}

bool TransferJob::InitDataStream(const SendFunc& send) {
    // fs.rs:893 init_data_stream
    if (OpenDataStream()) return false; // done
    if (type_ == JobType::Generic && enable_overwrite_detection_ && !file_confirmed_ &&
        !file_is_waiting_) {
        // 先置等待位再发送:send 回调若是同步投递(单测 loopback),对端的 confirm 会
        // 在 send 返回前重入 confirm() 修改状态;后置置位会覆盖该确认(rustdesk 异步
        // 发送无此问题,此处为同步调用场景的有意偏离)
        file_is_waiting_ = true;
        if (!SendCurrentDigest(send)) {
            // 通道忙:撤销等待位,下个 tick 重发
            file_is_waiting_ = false;
            return true;
        }
    }
    return true;
}

std::optional<px::FileTransferBlock> TransferJob::Read() {
    // fs.rs:929
    if (type_ == JobType::Generic) {
        if (enable_overwrite_detection_ && !file_confirmed_) return std::nullopt;
    }

    int32_t file_num = file_num_;
    std::string name;
    bool is_file_source = std::holds_alternative<std::filesystem::path>(data_source_);
    if (is_file_source) {
        size_t idx = static_cast<size_t>(file_num);
        if (idx >= files_.size()) {
            data_stream_.reset();
            return std::nullopt;
        }
        if (files_.size() == 1 && files_[idx].name().empty()) {
            // fs.rs:943 - 单文件空名场景用源文件名
            name = ToUtf8(std::get<std::filesystem::path>(data_source_).filename());
        } else {
            name = files_[idx].name();
        }
    }

    if (!data_stream_) Bail("data stream is None");

    std::vector<uint8_t> buf(kBlockPayloadSize);
    size_t offset = 0;
    while (true) {
        size_t n = 0;
        try {
            n = data_stream_->ReadSome(std::span<uint8_t>(buf).subspan(offset));
        } catch (...) {
            file_num_ += 1;
            data_stream_.reset();
            file_confirmed_ = false;
            file_is_waiting_ = false;
            throw;
        }
        offset += n;
        if (n == 0 || offset == buf.size()) break;
    }
    buf.resize(offset);

    if (offset == 0) {
        if (!is_file_source) {
            // 内存流耗尽
            data_stream_.reset();
            return std::nullopt;
        }
        file_num_ += 1;
        data_stream_.reset();
        file_confirmed_ = false;
        file_is_waiting_ = false;
    } else {
        finished_size_ += offset;
        if (!read_hasher_) read_hasher_.emplace();
        read_hasher_->Update(buf);
        bool compressed = false;
        if (is_file_source && !IsCompressedFile(name)) {
            std::vector<uint8_t> tmp = Compress(buf);
            if (!tmp.empty() && tmp.size() < buf.size()) {
                buf = std::move(tmp);
                compressed = true;
            }
        }
        transferred_ += buf.size();

        px::FileTransferBlock block;
        block.set_id(id_);
        block.set_file_num(file_num);
        block.set_data(buf.data(), buf.size());
        block.set_compressed(compressed);
        block.set_blk_id(next_read_block_id_++);
        return block;
    }
    // fs.rs:1001 - 文件 EOF 时也返回一个**空数据块**(file_num 为旧值),
    // 写侧靠后续块的新 file_num 推进;作业完成由下一次 read 返回 nullopt 判定
    px::FileTransferBlock block;
    block.set_id(id_);
    block.set_file_num(file_num);
    block.set_compressed(false);
    block.set_blk_id(next_read_block_id_++);
    if (!read_hasher_) read_hasher_.emplace();
    block.set_file_hash(Sha256Bytes(read_hasher_->Finalize()));
    read_hasher_.reset();
    return block;
}

bool TransferJob::Confirm(const px::FileTransferSendConfirmRequest& req) {
    // fs.rs:1156
    if (file_num_ != req.file_num()) {
        // 续传以外的 confirm 恒走此分支(与上游注释一致),仅记录不处理
        return true;
    }
    if (req.has_skip()) {
        if (req.skip()) {
            set_file_skipped();
        } else {
            set_file_confirmed(true);
        }
    } else if (req.has_offset_blk()) {
        set_file_confirmed(true);
        // 注意:offset_blk 沿上游命名,实为字节偏移
        uint64_t offset_bytes = req.offset_blk();
        if (offset_bytes > 0) {
            SetStreamOffset(req.file_num(), offset_bytes);
        }
    }
    return true;
}

void TransferJob::SetStreamOffset(int32_t file_num, uint64_t offset_bytes) {
    // fs.rs:1105
    if (!std::holds_alternative<std::filesystem::path>(data_source_)) return;
    const auto& base = std::get<std::filesystem::path>(data_source_);
    size_t idx = static_cast<size_t>(file_num);
    if (idx >= files_.size()) return;
    auto path = ResolveEntryPath(base, files_[idx].name());
    if (!path) return;
    std::string file_path = ToUtf8(*path);
    std::string download_path = file_path + ".download";
    std::string digest_path = file_path + ".digest";

    std::optional<DataStream> f;
    std::error_code ec;
    bool download_exists = std::filesystem::exists(ToFsPath(download_path), ec);
    bool digest_exists = std::filesystem::exists(ToFsPath(digest_path), ec);
    try {
        if (download_exists && digest_exists) {
            // 写侧续传:.download + .digest 都在 -> 打开 .download 写流并定位
            f = DataStream::OpenForWriteNoTrunc(ToFsPath(download_path));
            write_hasher_.emplace();
            std::ifstream prefix(ToFsPath(download_path), std::ios::binary);
            std::array<std::uint8_t, 64 * 1024> buffer{};
            std::uint64_t remaining = offset_bytes;
            while (remaining > 0 && prefix) {
                const auto request = static_cast<std::streamsize>(
                    std::min<std::uint64_t>(remaining, buffer.size()));
                prefix.read(reinterpret_cast<char*>(buffer.data()), request);
                const auto count = static_cast<std::size_t>(prefix.gcount());
                write_hasher_->Update(std::span<const std::uint8_t>(buffer).first(count));
                remaining -= count;
                if (count == 0) break;
            }
            if (remaining != 0) return;
        } else if (std::filesystem::exists(*path, ec)) {
            // 读侧续传:正式文件存在 -> 打开读流并定位
            f = DataStream::OpenForRead(*path);
            read_hasher_.emplace();
            std::ifstream prefix(*path, std::ios::binary);
            std::array<std::uint8_t, 64 * 1024> buffer{};
            std::uint64_t remaining = offset_bytes;
            while (remaining > 0 && prefix) {
                const auto request = static_cast<std::streamsize>(
                    std::min<std::uint64_t>(remaining, buffer.size()));
                prefix.read(reinterpret_cast<char*>(buffer.data()), request);
                const auto count = static_cast<std::size_t>(prefix.gcount());
                read_hasher_->Update(std::span<const std::uint8_t>(buffer).first(count));
                remaining -= count;
                if (count == 0) break;
            }
            if (remaining != 0) return;
        } else {
            return; // 文件不存在,无法定位
        }
    } catch (...) {
        return; // fs.rs:1127/1136 warn 后返回
    }
    if (f->SeekStart(offset_bytes)) {
        data_stream_ = std::move(f);
        transferred_ += offset_bytes;
        finished_size_ += offset_bytes;
    }
}

// ---------------- 写侧 ----------------

void TransferJob::Write(const px::FileTransferBlock& block) {
    // fs.rs:760
    if (block.id() != id_) Bail("Wrong id");
    if (std::holds_alternative<std::filesystem::path>(data_source_)) {
        const auto& base = std::get<std::filesystem::path>(data_source_);
        size_t file_num = static_cast<size_t>(block.file_num());
        if (file_num >= files_.size()) Bail("Wrong file number");
        if (file_num != static_cast<size_t>(file_num_) || !data_stream_) {
            ModifyTime(); // 收尾上一文件
            if (data_stream_) data_stream_->SyncAll();
            file_num_ = block.file_num();
            const px::FileEntry& entry = files_[file_num];
            std::string path;
            std::optional<std::string> digest_path;
            if (type_ == JobType::Printer) {
                path = ToUtf8(base);
            } else {
                // 注意:与上游一致保留"路径校验 + 普通打开"方式,
                // 仍存在已知 TOCTOU 窗口(fs.rs:781 注释),后续加固需句柄级 no-follow 打开。
                std::filesystem::path joined = JoinValidatedPath(base, entry.name());
                std::error_code ec;
                std::filesystem::path parent = joined.parent_path();
                if (!parent.empty()) std::filesystem::create_directories(parent, ec);
                path = ToUtf8(joined) + ".download";
                digest_path = ToUtf8(joined) + ".digest";
            }
            if (digest_path && IsFileExists(*digest_path)) {
                std::error_code ec;
                std::filesystem::remove(ToFsPath(*digest_path), ec);
                if (ec) Bail(ec.message());
            }
            data_stream_ = DataStream::CreateForWrite(ToFsPath(path));
            if (digest_path) WriteDigestFile(*digest_path, digest_);
            write_hasher_.emplace();
            next_write_block_id_ = 1;
            write_integrity_verified_ = false;
        }
    } else {
        if (!data_stream_) {
            auto& cursor = std::get<MemoryCursor>(data_source_);
            MemoryCursor owned;
            std::swap(owned, cursor);
            owned.read_pos = 0;
            data_stream_ = DataStream::FromMemory(std::move(owned));
        }
    }
    if (!data_stream_) Bail("data stream is None");
    if ((peer_capabilities_ & kFtCapabilityBlockSequence) != 0) {
        if (block.blk_id() != next_write_block_id_) {
            Bail("file transfer block sequence mismatch: expected " +
                 std::to_string(next_write_block_id_) + ", got " +
                 std::to_string(block.blk_id()));
        }
        ++next_write_block_id_;
    }
    std::vector<uint8_t> uncompressed;
    if (block.compressed()) {
        const std::vector<std::uint8_t> compressed(block.data().begin(), block.data().end());
        uncompressed = Decompress(compressed);
    } else {
        uncompressed.assign(block.data().begin(), block.data().end());
    }
    data_stream_->WriteAll(uncompressed);
    finished_size_ += uncompressed.size();
    if (!write_hasher_) write_hasher_.emplace();
    write_hasher_->Update(uncompressed);
    transferred_ += block.data().size();

    if (block.data().empty() && (peer_capabilities_ & kFtCapabilitySha256) != 0) {
        if (block.file_hash().size() != kSha256Size) {
            Bail("file transfer SHA-256 is missing from EOF block");
        }
        const auto actual = Sha256Bytes(write_hasher_->Finalize());
        write_hasher_.reset();
        if (actual != block.file_hash()) {
            Bail("file transfer SHA-256 mismatch");
        }
        write_integrity_verified_ = true;
    }
}

void TransferJob::ModifyTime() {
    // fs.rs:704
    if (type_ == JobType::Printer) return;
    if (!std::holds_alternative<std::filesystem::path>(data_source_)) return;
    const auto& base = std::get<std::filesystem::path>(data_source_);
    size_t file_num = static_cast<size_t>(file_num_);
    if (file_num >= files_.size()) return;
    const px::FileEntry& entry = files_[file_num];
    auto path = ResolveEntryPath(base, entry.name());
    if (!path) return;
    std::string file_path = ToUtf8(*path);
    // Windows 下打开中的文件不能 rename/delete:先关闭流(偏离 fs.rs 处说明:
    // 上游 tokio 文件句柄在 rename 时仍持有,依赖 Unix 语义;Win32 必须显式关闭)
    std::filesystem::path download = ToFsPath(file_path + ".download");
    std::error_code ec;
    const bool had_download = std::filesystem::exists(download, ec);
    if (had_download && (peer_capabilities_ & kFtCapabilitySha256) != 0 &&
        !write_integrity_verified_) {
        Bail("file transfer completed before SHA-256 verification");
    }
    if (data_stream_ && had_download) {
        data_stream_->SyncAll();
        data_stream_.reset();
    }
    // rename 成功才删 .digest(上游 fs.rs:717-718 先删凭证再 .ok() 静默吞 rename
    // 失败,此处有意偏离):失败时保留 .download/.digest 供断点续传,并抛错让作业
    // 以错误终结,由引擎走错误回调/错误消息路径(msg::new_error 语义)
    ec.clear();
    std::filesystem::rename(download, *path, ec);
    if (ec) {
        if (had_download) {
            Bail("failed to rename " + ToUtf8(download) + " to " + file_path + ": " +
                 ec.message());
        }
        return; // 本就没有 .download,保持上游静默语义
    }
    std::filesystem::remove(ToFsPath(file_path + ".digest"), ec);
    SetFileMtimeSecs(*path, entry.modified_time());
    write_hasher_.reset();
    write_integrity_verified_ = false;
}

void TransferJob::RemoveDownloadFile() {
    // fs.rs:728
    if (type_ == JobType::Printer) return;
    if (!std::holds_alternative<std::filesystem::path>(data_source_)) return;
    const auto& base = std::get<std::filesystem::path>(data_source_);
    size_t file_num = static_cast<size_t>(file_num_);
    if (file_num >= files_.size()) return;
    auto path = ResolveEntryPath(base, files_[file_num].name());
    if (!path) return;
    std::string file_path = ToUtf8(*path);
    // 同上:先关闭流再删除
    data_stream_.reset();
    std::error_code ec;
    std::filesystem::remove(ToFsPath(file_path + ".download"), ec);
    ec.clear();
    std::filesystem::remove(ToFsPath(file_path + ".digest"), ec);
}

void TransferJob::SetFinishedSizeOnResume() {
    // fs.rs:748
    if (is_resume && file_num_ > 0) {
        uint64_t sum = 0;
        for (int32_t i = 0; i < file_num_ && static_cast<size_t>(i) < files_.size(); ++i) {
            sum += files_[i].size();
        }
        finished_size_ = sum;
    }
}

// ---------------- 状态 ----------------

void TransferJob::set_file_confirmed(bool v) {
    // fs.rs:1042
    file_confirmed_ = v;
    file_skipped_ = false;
}

bool TransferJob::set_file_skipped() {
    // fs.rs:1095
    data_stream_.reset();
    set_file_confirmed(false);
    set_file_is_waiting(false);
    file_num_ += 1;
    file_skipped_ = true;
    return true;
}

std::optional<std::filesystem::path> TransferJob::ResolveEntryPath(
    const std::filesystem::path& base, const std::string& name) const {
    // fs.rs:690
    if (type_ == JobType::Generic) {
        try {
            return JoinValidatedPath(base, name);
        } catch (...) {
            return std::nullopt;
        }
    }
    return Join(base, name);
}

// ---------------- 作业表操作 ----------------

std::optional<TransferJob> RemoveJob(int32_t id, std::vector<TransferJob>& jobs) {
    for (auto it = jobs.begin(); it != jobs.end(); ++it) {
        if (it->id() == id) {
            TransferJob job = std::move(*it);
            jobs.erase(it);
            return job;
        }
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<TransferJob>> GetJob(
    int32_t id, std::vector<TransferJob>& jobs) {
    for (auto& job : jobs) {
        if (job.id() == id) return std::ref(job);
    }
    return std::nullopt;
}

std::optional<std::reference_wrapper<const TransferJob>> GetJob(
    int32_t id, const std::vector<TransferJob>& jobs) {
    for (const auto& job : jobs) {
        if (job.id() == id) return std::cref(job);
    }
    return std::nullopt;
}

// ---------------- Digest 覆盖决策 ----------------

DigestCheckResult IsWriteNeedConfirmation(bool is_resume, const std::string& file_path,
                                          const px::FileTransferDigest& digest) {
    // fs.rs:1449
    namespace fs = std::filesystem;
    fs::path path = ToFsPath(file_path);
    std::string digest_file = file_path + ".digest";
    std::string download_file = file_path + ".download";

    std::error_code ec;
    bool digest_exists = fs::exists(ToFsPath(digest_file), ec);
    bool download_exists = fs::exists(ToFsPath(download_file), ec);
    if (is_resume && digest_exists && download_exists) {
        // .digest 存在说明该文件此前传过,用凭证比对是否同一文件
        if (const auto local_digest = ReadDigestFile(digest_file)) {
            bool is_identical = local_digest->modified == digest.last_modified() &&
                                local_digest->size == digest.file_size();
            if (is_identical) {
                uint64_t transferred_size = fs::file_size(ToFsPath(download_file), ec);
                if (!ec && transferred_size > 0) {
                    // 已传部分非空才需要确认续传
                    DigestCheckResult res;
                    res.kind = DigestCheckResult::Kind::NeedConfirm;
                    res.digest.set_id(digest.id());
                    res.digest.set_file_num(digest.file_num());
                    res.digest.set_last_modified(digest.last_modified());
                    res.digest.set_file_size(digest.file_size());
                    res.digest.set_is_identical(true);
                    res.digest.set_transferred_size(transferred_size);
                    return res;
                }
            }
        }
    }

    if (fs::exists(path, ec) && fs::is_regular_file(path, ec)) {
        uint64_t size = fs::file_size(path, ec);
        if (ec) Bail(ec.message());
        uint64_t local_mt = GetFileMtimeSecs(path);
        // 是否覆盖交由用户决策(对齐系统文件管理器行为,fs.rs:1492 注释)
        bool is_identical = local_mt == digest.last_modified() && size == digest.file_size();
        DigestCheckResult res;
        res.kind = DigestCheckResult::Kind::NeedConfirm;
        res.digest.set_id(digest.id());
        res.digest.set_file_num(digest.file_num());
        res.digest.set_last_modified(local_mt);
        res.digest.set_file_size(size);
        res.digest.set_is_identical(is_identical);
        return res;
    }
    // 文件不存在,或 digest/download 不齐全
    DigestCheckResult res;
    res.kind = DigestCheckResult::Kind::NoSuchFile;
    return res;
}

// ---------------- 消息构造 ----------------

px::Message NewError(int32_t id, const std::string& err, int32_t file_num) {
    px::Message msg;
    auto& error = *msg.mutable_file_response()->mutable_error();
    error.set_id(id);
    error.set_error(err);
    error.set_file_num(file_num);
    return msg;
}

px::Message NewDir(int32_t id, std::string path, std::vector<px::FileEntry> files) {
    px::Message msg;
    auto& dir = *msg.mutable_file_response()->mutable_dir();
    dir.set_id(id);
    dir.set_path(std::move(path));
    for (auto& f : files) *dir.add_entries() = std::move(f);
    return msg;
}

px::Message NewBlock(px::FileTransferBlock block) {
    px::Message msg;
    *msg.mutable_file_response()->mutable_block() = std::move(block);
    return msg;
}

px::Message NewSendConfirm(px::FileTransferSendConfirmRequest req) {
    px::Message msg;
    *msg.mutable_file_action()->mutable_send_confirm() = std::move(req);
    return msg;
}

px::Message NewReceive(int32_t id, std::string path, int32_t file_num,
                       std::vector<px::FileEntry> files, uint64_t total_size) {
    px::Message msg;
    auto& request = *msg.mutable_file_action()->mutable_receive();
    request.set_id(id);
    request.set_path(std::move(path));
    request.set_file_num(file_num);
    request.set_total_size(total_size);
    for (auto& f : files) *request.add_files() = std::move(f);
    return msg;
}

px::Message NewSend(int32_t id, JobType type, std::string path, int32_t file_num,
                    bool include_hidden) {
    px::Message msg;
    auto& request = *msg.mutable_file_action()->mutable_send();
    request.set_id(id);
    request.set_path(std::move(path));
    request.set_include_hidden(include_hidden);
    request.set_file_num(file_num);
    request.set_file_type(type == JobType::Printer
                              ? px::FileTransferSendRequest_FileType_Printer
                              : px::FileTransferSendRequest_FileType_Generic);
    return msg;
}

px::Message NewDone(int32_t id, int32_t file_num) {
    px::Message msg;
    auto& done = *msg.mutable_file_response()->mutable_done();
    done.set_id(id);
    done.set_file_num(file_num);
    return msg;
}

px::Message NewCancel(int32_t id) {
    px::Message msg;
    msg.mutable_file_action()->mutable_cancel()->set_id(id);
    return msg;
}

} // namespace px::ft
