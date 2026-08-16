#include "ft_engine.h"

#include <atomic>
#include <cstdio>
#include <stdexcept>

namespace px::ft {

namespace {
std::atomic<int32_t> g_next_job_id{1}; // fs.rs:25 NEXT_JOB_ID

px::FileTransferSendConfirmRequest MakeConfirm(int32_t id, int32_t file_num, bool overwrite,
                                               uint64_t offset_bytes) {
    px::FileTransferSendConfirmRequest req;
    req.set_id(id);
    req.set_file_num(file_num);
    if (overwrite) {
        // 注意:offset_blk 沿上游命名,实为字节偏移
        req.set_offset_blk(static_cast<uint32_t>(offset_bytes));
    } else {
        req.set_skip(true);
    }
    return req;
}
} // namespace

int32_t FtEngine::NextJobId() { return g_next_job_id.fetch_add(1); }

FtEngine::FtEngine(SendFunc send)
    : send_(std::move(send)),
      last_refill_(std::chrono::steady_clock::now()),
      last_status_time_(std::chrono::steady_clock::now()) {
    if (!send_) throw std::invalid_argument("FtEngine: send callback is required");
}

void FtEngine::Log(const std::string& msg) {
    if (log_cb_) {
        log_cb_(msg);
    } else {
        std::fprintf(stderr, "[px_ft_engine] %s\n", msg.c_str());
    }
}

void FtEngine::SetRateLimitBytesPerSec(uint64_t bps) {
    rate_bps_ = bps;
    bucket_tokens_ = static_cast<double>(bps);
    last_refill_ = std::chrono::steady_clock::now();
}

bool FtEngine::Send(const px::Message& msg) {
    // 队列非空时直接入队保序
    if (!outbox_.empty()) {
        outbox_.push_back(msg);
        return false;
    }
    if (send_(msg)) return true;
    outbox_.push_back(msg);
    return false;
}

bool FtEngine::FlushOutbox() {
    while (!outbox_.empty()) {
        if (!send_(outbox_.front())) return false;
        outbox_.pop_front();
    }
    return true;
}

void FtEngine::Tick() {
    // 1. 冲刷待发队列;冲不动说明通道忙,本 tick 不读盘、不积块
    if (!FlushOutbox()) {
        return;
    }

    // 2. 限速令牌补充(上限 1s 的流量)
    if (rate_bps_ > 0) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - last_refill_).count();
        last_refill_ = now;
        bucket_tokens_ += elapsed * static_cast<double>(rate_bps_);
        // 桶上限至少容纳一个块,否则限速值小于块大小时永远发不出
        double cap = static_cast<double>(rate_bps_);
        if (cap < static_cast<double>(kBlockPayloadSize)) cap = kBlockPayloadSize;
        if (bucket_tokens_ > cap) bucket_tokens_ = cap;
    }

    // 3. init_jobs(fs.rs:1322):所有非挂起读作业初始化(覆盖检测 -> 发 Digest)
    for (auto& job : read_jobs_) {
        if (job.is_last_job) continue;
        try {
            job.InitDataStream([this](const px::Message& m) { return Send(m); });
        } catch (const std::exception& e) {
            Send(NewError(job.id(), e.what(), job.file_num()));
        }
    }

    // 4. 推进一个非等待作业一块(fs.rs:1344-1376,break 语义保留)
    std::vector<int32_t> finished;
    for (auto& job : read_jobs_) {
        if (job.is_last_job) continue;
        // 限速:桶内令牌不足一块时不读盘
        if (rate_bps_ > 0 && bucket_tokens_ < static_cast<double>(kBlockPayloadSize)) {
            break;
        }
        std::optional<px::FileTransferBlock> block;
        try {
            block = job.Read();
        } catch (const std::exception& e) {
            Send(NewError(job.id(), e.what(), job.file_num()));
        }
        if (block) {
            if (rate_bps_ > 0) {
                bucket_tokens_ -= static_cast<double>(block->data().size());
                if (bucket_tokens_ < 0) bucket_tokens_ = 0;
            }
            Send(NewBlock(std::move(*block)));
        } else if (job.job_completed()) {
            finished.push_back(job.id());
            std::string err = job.job_error().value_or("");
            if (!err.empty()) {
                Send(NewError(job.id(), err, job.file_num()));
            } else {
                Send(NewDone(job.id(), job.file_num()));
            }
            if (job_done_cb_) job_done_cb_(job.id(), job.file_num(), err);
        }
        // fs.rs:1376 - 每 tick 只推进一个作业
        break;
    }
    for (int32_t id : finished) {
        RemoveJob(id, read_jobs_);
    }

    // 5. 每秒进度回调(io_loop.rs:1048)
    UpdateJobsStatus();
}

void FtEngine::UpdateJobsStatus() {
    auto now = std::chrono::steady_clock::now();
    double elapsed_ms = std::chrono::duration<double, std::milli>(now - last_status_time_).count();
    if (elapsed_ms < 1000.0) return;
    last_status_time_ = now;
    if (!progress_cb_) {
        // 仍要更新基线,避免首次挂回调时爆发一个假速度
        for (auto& job : read_jobs_) last_transferred_[job.id()] = job.transferred();
        for (auto& job : write_jobs_) last_transferred_[job.id()] = job.transferred();
        return;
    }
    auto report = [&](TransferJob& job) {
        uint64_t transferred = job.transferred();
        uint64_t last = 0;
        auto it = last_transferred_.find(job.id());
        if (it != last_transferred_.end()) last = it->second;
        last_transferred_[job.id()] = transferred;
        TransferJobStatus st;
        st.id = job.id();
        st.file_num = job.file_num() > 0 ? job.file_num() - 1 : 0; // io_loop.rs:1044
        st.file_count = static_cast<int32_t>(job.files().size());
        st.total_size = job.total_size();
        st.finished_size = job.finished_size();
        st.transferred = transferred;
        st.speed = static_cast<double>(transferred - last) / (elapsed_ms / 1000.0);
        st.is_remote = job.is_remote;
        progress_cb_(st);
    };
    for (auto& job : read_jobs_) report(job);
    for (auto& job : write_jobs_) report(job);
}

// ---------------- 对端消息入口 ----------------

void FtEngine::HandleFileAction(const px::FileAction& action, const std::string& conn_id) {
    using U = px::FileAction::UnionCase;
    switch (action.union_case()) {
        case U::kReadDir: {
            const auto& rd = action.read_dir();
            // rustdesk: 空路径 -> 主目录(ui_cm_interface.rs:1537)
            std::string path = rd.path().empty() ? GetHomeAsString() : rd.path();
            try {
                px::FileDirectory fd = px::ft::ReadDir(path, rd.include_hidden());
                Send(NewDir(fd.id(), fd.path(),
                            std::vector<px::FileEntry>(fd.entries().begin(), fd.entries().end())));
            } catch (const std::exception& e) {
                // fs.rs read_dir 失败静默(上游 spawn_blocking 忽略 Err)
                Log(std::string("read_dir failed: ") + e.what());
            }
            break;
        }
        case U::kReadEmptyDirs: {
            const auto& rd = action.read_empty_dirs();
            try {
                auto fds = GetEmptyDirsRecursive(rd.path(), rd.include_hidden());
                px::Message msg;
                auto* resp = msg.mutable_file_response()->mutable_empty_dirs();
                resp->set_path(rd.path());
                for (auto& fd : fds) *resp->add_empty_dirs() = std::move(fd);
                Send(msg);
            } catch (const std::exception& e) {
                Log(std::string("read_empty_dirs failed: ") + e.what());
            }
            break;
        }
        case U::kAllFiles: {
            const auto& f = action.all_files();
            try {
                auto files = GetRecursiveFiles(f.path(), f.include_hidden());
                Send(NewDir(f.id(), f.path(), std::move(files)));
            } catch (const std::exception& e) {
                Send(NewError(f.id(), e.what(), -1));
            }
            break;
        }
        case U::kSend: {
            // 对端请求我方发送文件 -> 本地建读作业
            const auto& s = action.send();
            JobType type = s.file_type() == px::FileTransferSendRequest_FileType_Printer
                               ? JobType::Printer
                               : JobType::Generic;
            try {
                TransferJob job = TransferJob::NewRead(s.id(), type, s.path(),
                                                       DataSource{ToFsPath(s.path())}, s.file_num(),
                                                       s.include_hidden(), true, true);
                job.set_conn_id(conn_id);
                // connection.rs:5295 - 先把展开后的文件列表回给对端(对端写作业 set_files 用)
                Send(NewDir(job.id(), s.path(), job.files()));
                read_jobs_.push_back(std::move(job));
            } catch (const std::exception& e) {
                Send(NewError(s.id(), e.what(), s.file_num()));
            }
            break;
        }
        case U::kReceive: {
            // 对端要发文件给我 -> 本地建写作业
            const auto& r = action.receive();
            TransferJob job = TransferJob::NewWrite(r.id(), JobType::Generic, r.path(),
                                                    DataSource{ToFsPath(r.path())}, r.file_num(),
                                                    false, false, true);
            job.set_conn_id(conn_id);
            try {
                job.SetFiles(std::vector<px::FileEntry>(r.files().begin(), r.files().end()));
            } catch (const std::exception& e) {
                Log("Reject unsafe transfer file list for " + r.path() + ": " + e.what());
                Send(NewError(r.id(), e.what(), r.file_num()));
                break;
            }
            job.set_total_size(r.total_size()); // ui_cm_interface.rs:1027
            write_jobs_.push_back(std::move(job));
            break;
        }
        case U::kCreate: {
            const auto& c = action.create();
            try {
                px::ft::CreateDir(c.path());
                Send(NewDone(c.id(), 0));
            } catch (const std::exception& e) {
                Send(NewError(c.id(), e.what(), 0));
            }
            break;
        }
        case U::kRemoveDir: {
            const auto& d = action.remove_dir();
            try {
                if (d.recursive()) {
                    RemoveAllEmptyDir(ToFsPath(d.path()));
                } else {
                    std::error_code ec;
                    std::filesystem::remove(ToFsPath(d.path()), ec);
                    if (ec) throw std::runtime_error(ec.message());
                }
                Send(NewDone(d.id(), 0));
            } catch (const std::exception& e) {
                Send(NewError(d.id(), e.what(), 0));
            }
            break;
        }
        case U::kRemoveFile: {
            const auto& f = action.remove_file();
            try {
                px::ft::RemoveFile(f.path());
                Send(NewDone(f.id(), f.file_num()));
            } catch (const std::exception& e) {
                Send(NewError(f.id(), e.what(), f.file_num()));
            }
            break;
        }
        case U::kRename: {
            const auto& r = action.rename();
            try {
                px::ft::RenameFile(r.path(), r.new_name());
                Send(NewDone(r.id(), 0));
            } catch (const std::exception& e) {
                Send(NewError(r.id(), e.what(), 0));
            }
            break;
        }
        case U::kCancel: {
            int32_t id = action.cancel().id();
            // 写作业取消:清 .download/.digest(ui_cm_interface.rs:1036 CancelWrite)
            if (auto job = RemoveJob(id, write_jobs_)) {
                job->RemoveDownloadFile();
                if (job_done_cb_) job_done_cb_(id, job->file_num(), "cancel");
            }
            // 读作业取消:直接移除
            if (auto job = RemoveJob(id, read_jobs_)) {
                if (job_done_cb_) job_done_cb_(id, job->file_num(), "cancel");
            }
            break;
        }
        case U::kSendConfirm: {
            const auto& r = action.send_confirm();
            // 读侧作业(我方在发送)直接确认;否则落到写侧作业
            // (上传方向:主控 UI 决策后回 send_confirm,定位 .download 写流,
            // 对应 rustdesk CM 的 ipc::FS::SendConfirm 处理,ui_cm_interface.rs:1138)
            if (auto* job = GetJob(r.id(), read_jobs_)) {
                job->Confirm(r);
            } else if (auto* job = GetJob(r.id(), write_jobs_)) {
                job->Confirm(r);
            }
            break;
        }
        default:
            break;
    }
}

void FtEngine::HandleFileResponse(const px::FileResponse& resp) {
    using U = px::FileResponse::UnionCase;
    switch (resp.union_case()) {
        case U::kBlock:
            HandleBlock(resp.block());
            break;
        case U::kDone:
            HandleDone(resp.done());
            break;
        case U::kDigest:
            HandleDigest(resp.digest());
            break;
        case U::kError: {
            const auto& e = resp.error();
            // 写侧作业移除;保留 .download 供续传(ui_cm WriteError 语义)
            if (auto job = RemoveJob(e.id(), write_jobs_)) {
                if (job_done_cb_) job_done_cb_(e.id(), e.file_num(), e.error());
            } else if (response_cb_) {
                // 非作业语境的 error(create/remove/rename 等目录操作回执)透传上层。
                // 主控端 UI 据此刷新目录/提示失败;render 壳未设 response_cb_,行为不变。
                response_cb_(resp);
            }
            break;
        }
        case U::kDir: {
            // io_loop.rs:1520 - 若是对应写作业的文件列表(下载流程),先喂给作业
            const auto& fd = resp.dir();
            if (auto* job = GetJob(fd.id(), write_jobs_)) {
                try {
                    job->SetFiles(
                        std::vector<px::FileEntry>(fd.entries().begin(), fd.entries().end()));
                    job->SetFinishedSizeOnResume();
                } catch (const std::exception& e) {
                    Log("Reject unsafe file list from remote peer for job " +
                        std::to_string(fd.id()) + ": " + e.what());
                    CancelJob(fd.id());
                }
            }
            // 目录数据同时透传上层(对应 update_folder_files)
            if (response_cb_) response_cb_(resp);
            break;
        }
        case U::kEmptyDirs:
            // 数据类响应透传上层
            if (response_cb_) response_cb_(resp);
            break;
        default:
            break;
    }
}

void FtEngine::HandleBlock(const px::FileTransferBlock& block) {
    // io_loop.rs:1701
    if (auto* job = GetJob(block.id(), write_jobs_)) {
        try {
            job->Write(block);
        } catch (const std::exception& e) {
            // 写失败(含 Write 内收尾上一文件的 rename 失败):作业以错误终结,
            // .download/.digest 保留供续传,回 new_error 通知对端。
            // (上游 io_loop.rs:1703 仅忽略,此处选择显式失败,避免假进行中)
            Log(std::string("write block failed: ") + e.what());
            Send(NewError(block.id(), e.what(), block.file_num()));
            if (auto removed = RemoveJob(block.id(), write_jobs_)) {
                if (job_done_cb_) job_done_cb_(block.id(), removed->file_num(), e.what());
            }
            return;
        }
        if (job->type() == JobType::Generic) {
            UpdateJobsStatus(); // io_loop.rs:1707
        }
    }
}

void FtEngine::HandleDone(const px::FileTransferDone& done) {
    // io_loop.rs:1711
    if (auto job = RemoveJob(done.id(), write_jobs_)) {
        std::string err;
        try {
            job->ModifyTime();
            err = job->job_error().value_or("");
        } catch (const std::exception& e) {
            // 收尾 rename 失败:.download/.digest 已保留供续传,作业以错误终结。
            // 回 new_error 让对端(主控)感知失败,而非假成功(io_loop.rs new_error 语义)
            Log(std::string("finalize job ") + std::to_string(done.id()) +
                " failed: " + e.what());
            err = e.what();
            Send(NewError(done.id(), err, done.file_num()));
        }
        if (job_done_cb_) job_done_cb_(done.id(), done.file_num(), err);
    } else if (response_cb_) {
        // 非作业语境的 done(目录操作回执)透传上层,同 kError 分支。
        px::FileResponse resp;
        *resp.mutable_done() = done;
        response_cb_(resp);
    }
}

void FtEngine::HandleDigest(const px::FileTransferDigest& digest) {
    // io_loop.rs:1570-1699
    if (digest.is_upload()) {
        // 上传方向:我方是读侧;对端(写侧)报回它本地的同名文件情况
        auto* job = GetJob(digest.id(), read_jobs_);
        if (!job) return;
        if (digest.file_num() < 0 ||
            static_cast<size_t>(digest.file_num()) >= job->files().size()) {
            return;
        }
        const auto* p = std::get_if<std::filesystem::path>(&job->data_source());
        if (!p) return;
        std::string read_path =
            ToUtf8(TransferJob::Join(*p, job->files()[digest.file_num()].name()));
        std::optional<bool> overwrite_strategy = job->default_overwrite_strategy();
        uint64_t offset = 0;
        if (digest.is_identical() && job->is_resume && digest.transferred_size() > 0) {
            overwrite_strategy = true;
            offset = digest.transferred_size();
        }
        if (overwrite_strategy) {
            auto req = MakeConfirm(digest.id(), digest.file_num(), *overwrite_strategy, offset);
            job->Confirm(req);
            Send(NewSendConfirm(req));
        } else if (overwrite_confirm_cb_) {
            overwrite_confirm_cb_(digest.id(), digest.file_num(), read_path, true,
                                  digest.is_identical());
        }
    } else {
        // 下载方向:我方是写侧;对端(读侧)发来源文件 digest,本地做覆盖/续传决策
        auto* job = GetJob(digest.id(), write_jobs_);
        if (!job) return;
        if (digest.file_num() < 0 ||
            static_cast<size_t>(digest.file_num()) >= job->files().size()) {
            return;
        }
        const auto* p = std::get_if<std::filesystem::path>(&job->data_source());
        if (!p) return;
        // io_loop.rs:1618 - 此处用普通 join(写盘前 Write 内还会再过校验)
        std::string write_path =
            ToUtf8(TransferJob::Join(*p, job->files()[digest.file_num()].name()));
        job->set_digest(digest.file_size(), digest.last_modified());
        // 续传判定:被控侧写作业 is_resume 恒 false,必须用 digest 里的 is_resume
        // (ui_cm_interface.rs:1106 CheckDigest 参数语义);主控下载侧本地写作业可能带
        // is_resume(io_loop.rs ResumeJob),两者取或
        const bool is_resume = digest.is_resume() || job->is_resume;
        DigestCheckResult res;
        try {
            res = IsWriteNeedConfirmation(is_resume, write_path, digest);
        } catch (const std::exception& e) {
            Log(std::string("error receiving digest: ") + e.what());
            return;
        }
        switch (res.kind) {
            case DigestCheckResult::Kind::IsSame: {
                auto req = MakeConfirm(digest.id(), digest.file_num(), false, 0);
                job->Confirm(req);
                Send(NewSendConfirm(req));
                break;
            }
            case DigestCheckResult::Kind::NeedConfirm: {
                std::optional<bool> overwrite_strategy = job->default_overwrite_strategy();
                uint64_t offset = 0;
                if (res.digest.is_identical() && is_resume &&
                    res.digest.transferred_size() > 0) {
                    overwrite_strategy = true;
                    offset = res.digest.transferred_size();
                }
                if (overwrite_strategy) {
                    auto req =
                        MakeConfirm(digest.id(), digest.file_num(), *overwrite_strategy, offset);
                    job->Confirm(req);
                    Send(NewSendConfirm(req));
                } else if (job->is_remote) {
                    // 本端是主控(下载方向,io_loop.rs:1615 写侧语义):本地 UI 决策
                    if (overwrite_confirm_cb_) {
                        overwrite_confirm_cb_(digest.id(), digest.file_num(), write_path, false,
                                              res.digest.is_identical());
                    }
                } else {
                    // 本端是被控(上传方向,ui_cm_interface.rs:1116-1124 CheckDigest 语义):
                    // 回发 digest(is_upload=true)给主控,由主控 UI 弹框决策;
                    // 主控决策后回 send_confirm,经 kSendConfirm 落到本写作业
                    px::Message msg;
                    auto* out = msg.mutable_file_response()->mutable_digest();
                    *out = res.digest;
                    out->set_is_upload(true);
                    out->set_is_resume(digest.is_resume());
                    Send(msg);
                }
                break;
            }
            case DigestCheckResult::Kind::NoSuchFile: {
                auto req = MakeConfirm(digest.id(), digest.file_num(), true, 0);
                job->Confirm(req);
                Send(NewSendConfirm(req));
                break;
            }
        }
    }
}

// ---------------- 本端主动操作 ----------------

int32_t FtEngine::SendFiles(const std::string& local_path, bool include_hidden,
                            const std::string& remote_to, int32_t file_num, bool is_resume,
                            const std::string& conn_id) {
    int32_t id = NextJobId();
    try {
        TransferJob job = TransferJob::NewRead(id, JobType::Generic, remote_to,
                                               DataSource{ToFsPath(local_path)}, file_num,
                                               include_hidden, false, true);
        job.is_resume = is_resume;
        job.set_conn_id(conn_id);
        std::vector<px::FileEntry> files = job.files();
        uint64_t total_size = job.total_size();
        read_jobs_.push_back(std::move(job));
        Send(NewReceive(id, remote_to, file_num, std::move(files), total_size));
    } catch (const std::exception& e) {
        if (job_done_cb_) job_done_cb_(id, -1, e.what());
    }
    return id;
}

int32_t FtEngine::ReceiveFiles(const std::string& remote_path, bool include_hidden,
                               const std::string& local_to, int32_t file_num, bool is_resume,
                               const std::string& conn_id) {
    int32_t id = NextJobId();
    TransferJob job = TransferJob::NewWrite(id, JobType::Generic, remote_path,
                                            DataSource{ToFsPath(local_to)}, file_num,
                                            include_hidden, true, true);
    job.is_resume = is_resume;
    job.set_conn_id(conn_id);
    job.SetFinishedSizeOnResume(); // io_loop.rs:730 ResumeJob 语义
    write_jobs_.push_back(std::move(job));
    Send(NewSend(id, JobType::Generic, remote_path, file_num, include_hidden));
    return id;
}

void FtEngine::ReadDir(const std::string& path, bool include_hidden) {
    px::Message msg;
    auto* rd = msg.mutable_file_action()->mutable_read_dir();
    rd->set_path(path);
    rd->set_include_hidden(include_hidden);
    Send(msg);
}

void FtEngine::ReadAllFiles(int32_t id, const std::string& path, bool include_hidden) {
    px::Message msg;
    auto* f = msg.mutable_file_action()->mutable_all_files();
    f->set_id(id);
    f->set_path(path);
    f->set_include_hidden(include_hidden);
    Send(msg);
}

void FtEngine::ReadEmptyDirs(const std::string& path, bool include_hidden) {
    px::Message msg;
    auto* rd = msg.mutable_file_action()->mutable_read_empty_dirs();
    rd->set_path(path);
    rd->set_include_hidden(include_hidden);
    Send(msg);
}

void FtEngine::CreateDir(int32_t id, const std::string& path) {
    px::Message msg;
    auto* c = msg.mutable_file_action()->mutable_create();
    c->set_id(id);
    c->set_path(path);
    Send(msg);
}

void FtEngine::RemoveDir(int32_t id, const std::string& path, bool recursive) {
    px::Message msg;
    auto* d = msg.mutable_file_action()->mutable_remove_dir();
    d->set_id(id);
    d->set_path(path);
    d->set_recursive(recursive);
    Send(msg);
}

void FtEngine::RemoveFile(int32_t id, const std::string& path, int32_t file_num) {
    px::Message msg;
    auto* f = msg.mutable_file_action()->mutable_remove_file();
    f->set_id(id);
    f->set_path(path);
    f->set_file_num(file_num);
    Send(msg);
}

void FtEngine::RenameFile(int32_t id, const std::string& path, const std::string& new_name) {
    px::Message msg;
    auto* r = msg.mutable_file_action()->mutable_rename();
    r->set_id(id);
    r->set_path(path);
    r->set_new_name(new_name);
    Send(msg);
}

void FtEngine::CancelJob(int32_t id) {
    if (auto job = RemoveJob(id, write_jobs_)) {
        job->RemoveDownloadFile();
        if (job_done_cb_) job_done_cb_(id, job->file_num(), "cancel");
    }
    if (auto job = RemoveJob(id, read_jobs_)) {
        if (job_done_cb_) job_done_cb_(id, job->file_num(), "cancel");
    }
    Send(NewCancel(id));
}

void FtEngine::DisconnectCleanup(const std::string& conn_id) {
    // 断线:保留 .download/.digest 供续传,只清作业表。
    // conn_id 非空时只移除该连接的作业——迟到的断线事件不会误杀其他
    // (或同 stream id 新会话)作业;空 = 清全部(含待发队列)。
    if (conn_id.empty()) {
        read_jobs_.clear();
        write_jobs_.clear();
        outbox_.clear();
        return;
    }
    std::erase_if(read_jobs_, [&](const TransferJob& j) { return j.conn_id() == conn_id; });
    std::erase_if(write_jobs_, [&](const TransferJob& j) { return j.conn_id() == conn_id; });
}

void FtEngine::SetOverwriteStrategy(int32_t id, std::optional<bool> overwrite) {
    if (auto* job = GetJob(id, read_jobs_)) job->set_overwrite_strategy(overwrite);
    if (auto* job = GetJob(id, write_jobs_)) job->set_overwrite_strategy(overwrite);
}

void FtEngine::ConfirmFile(int32_t id, int32_t file_num, bool overwrite, uint64_t offset_bytes) {
    auto req = MakeConfirm(id, file_num, overwrite, offset_bytes);
    // 回喂本地作业状态(io_loop.rs override_file_confirm 后 confirm 语义)
    if (auto* job = GetJob(id, read_jobs_)) job->Confirm(req);
    if (auto* job = GetJob(id, write_jobs_)) job->Confirm(req);
    Send(NewSendConfirm(req));
}

} // namespace px::ft
