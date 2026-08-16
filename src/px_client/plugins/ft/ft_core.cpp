//
// ft 主控端插件 core — px_ft_engine 的薄适配层
//

#include "ft_core.h"
#include "ft_client_plugin.h"

#include <QDir>
#include <QFileInfo>

#include "px_message.pb.h"
#include "ft_engine.h"
#include "ft_path.h"
#include "px_common_new/log.h"

namespace px
{

    // rustdesk 调度节拍:有作业 1ms,空闲退避(MILLI1/SEC30 语义,空闲取 1s)
    static constexpr auto kTickBusy = std::chrono::milliseconds(1);
    static constexpr auto kTickIdle = std::chrono::milliseconds(1000);
    // 无作业但刚有活动(待发队列可能压着握手消息)时的冲刷宽限期
    static constexpr auto kActivityGrace = std::chrono::seconds(5);

    static QString NormalizeRemotePath(const QString& p) {
        QString r = p;
        r.replace('\\', '/');
        while (r.size() > 1 && r.endsWith('/')) {
            r.chop(1);
        }
        return r;
    }

    static QString RemoteBaseName(const QString& p) {
        const QString n = NormalizeRemotePath(p);
        const int idx = n.lastIndexOf('/');
        return idx >= 0 ? n.mid(idx + 1) : n;
    }

    static QString JoinRemote(const QString& dir, const QString& name) {
        const QString d = NormalizeRemotePath(dir);
        if (d.isEmpty() || d == "/") {
            return "/" + name;
        }
        // "C:" 这类盘符根:拼成 "C:/name"
        return d + "/" + name;
    }

    FtCore::FtCore(FtClientPlugin* plugin) : plugin_(plugin) {
        qRegisterMetaType<px::FtEntryInfo>("px::FtEntryInfo");
        qRegisterMetaType<QVector<px::FtEntryInfo>>("QVector<px::FtEntryInfo>");
        qRegisterMetaType<px::FtJobStatusInfo>("px::FtJobStatusInfo");
    }

    FtCore::~FtCore() {
        Stop();
    }

    void FtCore::Start() {
        engine_ = std::make_unique<px::ft::FtEngine>(
            [this](const px::Message& msg) { return plugin_->SendToChannel(msg); });
        engine_->SetLogCallback([](const std::string& msg) {
            LOGW("[ft_engine] {}", msg);
        });
        engine_->SetProgressCallback([this](const px::ft::TransferJobStatus& st) {
            FtJobStatusInfo info;
            info.id_ = st.id;
            info.file_num_ = st.file_num;
            info.file_count_ = st.file_count;
            info.total_size_ = st.total_size;
            info.finished_size_ = st.finished_size;
            info.speed_ = st.speed;
            info.is_download_ = st.is_remote;
            info.done_ = st.done;
            info.cancel_ = st.cancel;
            info.error_ = QString::fromStdString(st.error);
            emit SigJobProgress(info);
        });
        engine_->SetJobDoneCallback([this](int32_t job_id, int32_t file_num,
                                           const std::string& error_or_empty) {
            (void)file_num;
            emit SigJobDone(job_id, QString::fromStdString(error_or_empty));
        });
        engine_->SetOverwriteConfirmCallback(
            [this](int32_t job_id, int32_t file_num, const std::string& path,
                   bool is_upload, bool is_identical) {
                // worker 线程 emit -> UI 线程弹框;作业在引擎内等待 ConfirmFile 回喂
                emit SigOverwriteConfirm(job_id, file_num, QString::fromStdString(path),
                                         is_upload, is_identical);
            });
        engine_->SetResponseCallback([this](const px::FileResponse& resp) {
            ProcessResponse(resp);
        });

        last_activity_ = std::chrono::steady_clock::now();
        accepting_ = true;
        worker_ = std::thread([this]() { this->WorkerMain(); });
        LOGI("ft client core started.");
    }

    void FtCore::Stop() {
        accepting_ = false;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            if (!worker_exit_) {
                // 在途作业按取消语义处理(清 .download/.digest,并通知对端)
                tasks_.emplace_back([this]() {
                    if (engine_) {
                        std::vector<int32_t> ids;
                        for (const auto& job : engine_->read_jobs()) ids.push_back(job.id());
                        for (const auto& job : engine_->write_jobs()) ids.push_back(job.id());
                        for (int32_t id : ids) engine_->CancelJob(id);
                    }
                });
                worker_exit_ = true;
            }
        }
        task_cv_.notify_one();
        if (worker_.joinable()) {
            worker_.join();
        }
        engine_.reset();
    }

    // ---------------- 网络入口 ----------------

    void FtCore::EnqueueMessage(const std::shared_ptr<Message>& msg) {
        if (!accepting_.load()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, msg]() { this->ProcessMessage(msg); });
        }
        task_cv_.notify_one();
    }

    // ---------------- UI 主动操作 ----------------

    void FtCore::ReadDir(const QString& path) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, p = NormalizeRemotePath(path)]() {
                if (engine_) {
                    engine_->ReadDir(p.toStdString(), false);
                    last_activity_ = std::chrono::steady_clock::now();
                }
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::CreateDir(const QString& path) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, p = NormalizeRemotePath(path)]() {
                if (engine_) {
                    engine_->CreateDir(next_op_id_--, p.toStdString());
                    last_activity_ = std::chrono::steady_clock::now();
                }
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::RemoveEntry(const QString& path, bool is_dir) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, p = NormalizeRemotePath(path), is_dir]() {
                if (engine_) {
                    if (is_dir) {
                        engine_->RemoveDir(next_op_id_--, p.toStdString(), true);
                    } else {
                        engine_->RemoveFile(next_op_id_--, p.toStdString());
                    }
                    last_activity_ = std::chrono::steady_clock::now();
                }
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::RenameEntry(const QString& path, const QString& new_name) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, p = NormalizeRemotePath(path), n = new_name]() {
                if (engine_) {
                    engine_->RenameFile(next_op_id_--, p.toStdString(), n.toStdString());
                    last_activity_ = std::chrono::steady_clock::now();
                }
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::StartUpload(const QStringList& local_paths, const QString& remote_dir) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, local_paths, rd = NormalizeRemotePath(remote_dir)]() {
                if (!engine_) return;
                for (const auto& lp : local_paths) {
                    QFileInfo fi(lp);
                    const QString name = fi.fileName();
                    const QString remote_to = JoinRemote(rd, name);
                    // 本地建读作业(递归展开)+ 向对端发 FileTransferReceiveRequest
                    const int32_t id = engine_->SendFiles(lp.toStdString(), false,
                                                          remote_to.toStdString());
                    emit SigJobAdded(id, name, false);
                    // 目录上传:先补空目录(file_model.dart:570 语义)
                    if (fi.isDir()) {
                        CreateRemoteEmptyDirs(id, lp, remote_to);
                    }
                }
                last_activity_ = std::chrono::steady_clock::now();
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::StartDownload(const QStringList& remote_paths, const QString& local_dir) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, remote_paths, local_dir]() {
                if (!engine_) return;
                for (const auto& rp : remote_paths) {
                    const QString norm = NormalizeRemotePath(rp);
                    const QString name = RemoteBaseName(norm);
                    const QString local_to = QDir(local_dir).filePath(name);
                    // 本地建写作业 + 向对端发 FileTransferSendRequest
                    const int32_t id = engine_->ReceiveFiles(norm.toStdString(), false,
                                                             local_to.toStdString());
                    emit SigJobAdded(id, name, true);
                    // 目录下载:向远端要空目录清单,回包后在本地落地
                    // (对文件查询远端会返回空清单,无副作用)
                    engine_->ReadEmptyDirs(norm.toStdString(), false);
                    pending_empty_dirs_[norm.toStdString()] = local_to;
                }
                last_activity_ = std::chrono::steady_clock::now();
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::CancelJob(int32_t id) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, id]() {
                if (engine_) engine_->CancelJob(id);
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::ConfirmOverwrite(int32_t job_id, int32_t file_num, int choice,
                                  uint64_t offset, bool apply_to_all) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, job_id, file_num, choice, offset, apply_to_all]() {
                if (!engine_) return;
                if (apply_to_all) {
                    // "应用到全部冲突":skip=false / overwrite|resume=true(io_loop.rs:673)
                    engine_->SetOverwriteStrategy(job_id, choice != 0);
                }
                if (choice == 0) {
                    engine_->ConfirmFile(job_id, file_num, false, 0);
                } else if (choice == 2) {
                    engine_->ConfirmFile(job_id, file_num, true, offset);
                } else {
                    engine_->ConfirmFile(job_id, file_num, true, 0);
                }
            });
        }
        task_cv_.notify_one();
    }

    void FtCore::SetRateLimitBytesPerSec(uint64_t bps) {
        if (!accepting_.load()) return;
        {
            std::lock_guard<std::mutex> lk(task_mutex_);
            tasks_.emplace_back([this, bps]() {
                if (engine_) engine_->SetRateLimitBytesPerSec(bps);
            });
        }
        task_cv_.notify_one();
    }

    // ---------------- worker 线程 ----------------

    void FtCore::WorkerMain() {
        while (true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lk(task_mutex_);
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
                has_jobs_ = !engine_->read_jobs().empty() || !engine_->write_jobs().empty();
            }
        }
        has_jobs_ = false;
        LOGI("ft client core worker exited.");
    }

    void FtCore::ProcessMessage(const std::shared_ptr<Message>& msg) {
        if (!engine_) {
            return;
        }
        if (msg->type() == MessageType::kFileAction) {
            // 对称语义:对端也可能发起(io_loop.rs 双端同构)
            engine_->HandleFileAction(msg->file_action());
        } else if (msg->type() == MessageType::kFileResponse) {
            engine_->HandleFileResponse(msg->file_response());
        }
        last_activity_ = std::chrono::steady_clock::now();
    }

    void FtCore::ProcessResponse(const px::FileResponse& resp) {
        // 仅 worker 线程(引擎 response_cb_ 触发)
        using U = px::FileResponse::UnionCase;
        switch (resp.union_case()) {
            case U::kDir: {
                const auto& dir = resp.dir();
                if (dir.id() != 0) {
                    // 作业语境的文件列表(ReadAllFiles/下载握手),引擎已消费,UI 不展示
                    return;
                }
                emit SigRemoteDir(QString::fromStdString(dir.path()), ConvertEntries(dir));
                break;
            }
            case U::kEmptyDirs: {
                const auto& ed = resp.empty_dirs();
                auto it = pending_empty_dirs_.find(ed.path());
                if (it != pending_empty_dirs_.end()) {
                    CreateLocalEmptyDirs(ed, it->second);
                    pending_empty_dirs_.erase(it);
                }
                break;
            }
            case U::kDone: {
                const auto& d = resp.done();
                if (d.id() < 0) {
                    emit SigDirOpDone(d.id(), QString());
                }
                break;
            }
            case U::kError: {
                const auto& e = resp.error();
                if (e.id() < 0) {
                    emit SigDirOpDone(e.id(), QString::fromStdString(e.error()));
                } else {
                    // 作业级错误(job_done 未覆盖的,如读侧建作业失败)也通知 UI
                    emit SigJobDone(e.id(), QString::fromStdString(e.error()));
                }
                break;
            }
            default:
                break;
        }
    }

    QVector<FtEntryInfo> FtCore::ConvertEntries(const px::FileDirectory& dir) {
        QVector<FtEntryInfo> out;
        out.reserve(dir.entries().size());
        for (const auto& e : dir.entries()) {
            FtEntryInfo info;
            info.name_ = QString::fromStdString(e.name());
            info.is_dir_ = e.entry_type() == px::FileType::Dir;
            info.is_drive_ = e.entry_type() == px::FileType::DirDrive;
            info.size_ = e.size();
            info.modified_time_ = (int64_t)e.modified_time();
            out.push_back(info);
        }
        return out;
    }

    void FtCore::CreateRemoteEmptyDirs(int32_t job_id, const QString& local_path,
                                       const QString& remote_to) {
        // 本地递归找空目录(file_model.dart:621 readEmptyDirs(isLocal=true) 语义),
        // 直接用引擎同款实现,保证两端语义一致。
        std::vector<px::FileDirectory> fds;
        try {
            fds = px::ft::GetEmptyDirsRecursive(local_path.toStdString(), false);
        } catch (const std::exception& e) {
            LOGW("read local empty dirs failed: {}, {}", local_path.toStdString(), e.what());
            return;
        }
        for (const auto& fd : fds) {
            QString p = QString::fromStdString(fd.path());
            p.replace('\\', '/');
            // fd.path 为相对前缀;根目录本身为空时是完整路径,此时建 remote_to 根
            const bool is_root = QDir::isAbsolutePath(p) || p.contains(':');
            const QString target = is_root ? remote_to : JoinRemote(remote_to, p);
            engine_->CreateDir(job_id, target.toStdString());
        }
    }

    void FtCore::CreateLocalEmptyDirs(const px::ReadEmptyDirsResponse& resp,
                                      const QString& local_dir) {
        // fd.path 语义同上:相对前缀(根为空时为完整远端路径)
        for (const auto& fd : resp.empty_dirs()) {
            QString rel = QString::fromStdString(fd.path());
            rel.replace('\\', '/');
            if (rel.isEmpty() || QDir::isAbsolutePath(rel) || rel.contains(':')) {
                // 根目录本身:local_dir 落点已含目录名,mkpath 一次即可
                QDir().mkpath(local_dir);
                continue;
            }
            QDir().mkpath(QDir(local_dir).filePath(rel));
        }
    }

}
