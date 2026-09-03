//
// ft 主控端内置模块 core — px_ft_engine 的薄适配层
//

#include "ft_core.h"

#include <QDir>
#include <QFileInfo>
#include <QPointer>
#include <utility>

#include "px_message.pb.h"
#include "ft_async_session.h"
#include "ft_engine.h"
#include "ft_path.h"
#include "px_common_new/log.h"

namespace px
{

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

    FtCore::FtCore(SendCallback send_callback)
        : send_callback_(std::move(send_callback)) {
        qRegisterMetaType<px::FtEntryInfo>("px::FtEntryInfo");
        qRegisterMetaType<px::FtEntryList>("px::FtEntryList");
        qRegisterMetaType<px::FtJobStatusInfo>("px::FtJobStatusInfo");
    }

    FtCore::~FtCore() {
        Stop();
    }

    void FtCore::Start() {
        const QPointer<FtCore> self(this);
        const auto session = px::ft::FtAsyncSession::Create(
            [self](const auto& message) {
                if (!self || !self->send_callback_) {
                    return FileTransferSendResult::Disconnected(
                        "FT client core was destroyed");
                }
                return self->send_callback_(*message);
            },
            [self](const auto& engine) {
                engine->SetLogCallback([](const std::string& message) {
                    LOGW("[ft_engine] {}", message);
                });
                engine->SetProgressCallback([self](const px::ft::TransferJobStatus& status) {
                    if (!self) return;
                    FtJobStatusInfo info;
                    info.id_ = status.id;
                    info.file_num_ = status.file_num;
                    info.file_count_ = status.file_count;
                    info.total_size_ = status.total_size;
                    info.finished_size_ = status.finished_size;
                    info.speed_ = status.speed;
                    info.is_download_ = status.is_remote;
                    info.done_ = status.done;
                    info.cancel_ = status.cancel;
                    info.error_ = QString::fromStdString(status.error);
                    emit self->SigJobProgress(info);
                });
                engine->SetJobDoneCallback(
                    [self](int32_t job_id, int32_t file_num, const std::string& error) {
                        static_cast<void>(file_num);
                        if (self) emit self->SigJobDone(job_id, QString::fromStdString(error));
                    });
                engine->SetOverwriteConfirmCallback(
                    [self](int32_t job_id, int32_t file_num, const std::string& path,
                           bool is_upload, bool is_identical) {
                        if (self) {
                            emit self->SigOverwriteConfirm(
                                job_id, file_num, QString::fromStdString(path),
                                is_upload, is_identical);
                        }
                    });
                engine->SetResponseCallback([self](const px::FileResponse& response) {
                    if (self) self->ProcessResponse(response);
                });
            });
        if (!session->Start()) {
            LOGE("ft client async session failed to start.");
            return;
        }
        session_.store(session);
        accepting_ = true;
        LOGI("ft client async session started.");
    }

    void FtCore::Stop() {
        accepting_ = false;
        const auto session = session_.exchange({});
        if (session) {
            static_cast<void>(session->PostAndWait(
                "ft-client-cancel-before-stop",
                [](const auto& engine) {
                    std::vector<int32_t> ids;
                    for (const auto& job : engine->read_jobs()) ids.push_back(job.id());
                    for (const auto& job : engine->write_jobs()) ids.push_back(job.id());
                    for (const auto id : ids) engine->CancelJob(id);
                }, std::chrono::seconds(5)));
            if (!session->StopAndWait(std::chrono::seconds(5))) {
                LOGE("ft client async session did not stop within deadline");
            }
        }
    }

    bool FtCore::HasJobs() const {
        const auto session = session_.load();
        return session && session->HasJobs();
    }

    // ---------------- 网络入口 ----------------

    void FtCore::EnqueueMessage(const std::shared_ptr<Message>& msg) {
        const QPointer<FtCore> self(this);
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        static_cast<void>(session->Post("ft-client-inbound", [self, msg](const auto& engine) {
            if (self) self->ProcessMessage(engine, msg);
        }));
    }

    // ---------------- UI 主动操作 ----------------

    void FtCore::ReadDir(const QString& path) {
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        const auto normalized = NormalizeRemotePath(path);
        static_cast<void>(session->Post("ft-read-dir", [normalized](const auto& engine) {
            LOGI("ft remote directory request: {}", normalized.toStdString());
            engine->ReadDir(normalized.toStdString(), false);
        }));
    }

    void FtCore::CreateDir(const QString& path) {
        const QPointer<FtCore> self(this);
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        const auto normalized = NormalizeRemotePath(path);
        static_cast<void>(session->Post("ft-create-dir", [self, normalized](const auto& engine) {
            if (self) engine->CreateDir(self->next_op_id_--, normalized.toStdString());
        }));
    }

    void FtCore::RemoveEntry(const QString& path, bool is_dir) {
        const QPointer<FtCore> self(this);
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        const auto normalized = NormalizeRemotePath(path);
        static_cast<void>(session->Post(
            "ft-remove-entry", [self, normalized, is_dir](const auto& engine) {
                if (!self) return;
                if (is_dir) engine->RemoveDir(self->next_op_id_--, normalized.toStdString(), true);
                else engine->RemoveFile(self->next_op_id_--, normalized.toStdString());
            }));
    }

    void FtCore::RenameEntry(const QString& path, const QString& new_name) {
        const QPointer<FtCore> self(this);
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        const auto normalized = NormalizeRemotePath(path);
        static_cast<void>(session->Post(
            "ft-rename-entry", [self, normalized, new_name](const auto& engine) {
                if (self) engine->RenameFile(
                    self->next_op_id_--, normalized.toStdString(), new_name.toStdString());
            }));
    }

    void FtCore::StartUpload(const QStringList& local_paths, const QString& remote_dir) {
        const QPointer<FtCore> self(this);
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        const auto normalized_dir = NormalizeRemotePath(remote_dir);
        static_cast<void>(session->Post(
            "ft-start-upload",
            [self, local_paths, normalized_dir](const auto& engine) {
                if (!self) return;
                for (const auto& lp : local_paths) {
                    QFileInfo fi(lp);
                    const QString name = fi.fileName();
                    const QString remote_to = JoinRemote(normalized_dir, name);
                    // 本地建读作业(递归展开)+ 向对端发 FileTransferReceiveRequest
                    const int32_t id = engine->SendFiles(
                        lp.toStdString(), false, remote_to.toStdString());
                    emit self->SigJobAdded(id, name, false);
                    // 目录上传:先补空目录(file_model.dart:570 语义)
                    if (fi.isDir()) {
                        self->CreateRemoteEmptyDirs(engine, id, lp, remote_to);
                    }
                }
            }));
    }

    void FtCore::StartDownload(const QStringList& remote_paths, const QString& local_dir) {
        const QPointer<FtCore> self(this);
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        static_cast<void>(session->Post(
            "ft-start-download", [self, remote_paths, local_dir](const auto& engine) {
                if (!self) return;
                for (const auto& rp : remote_paths) {
                    const QString norm = NormalizeRemotePath(rp);
                    const QString name = RemoteBaseName(norm);
                    const QString local_to = QDir(local_dir).filePath(name);
                    // 本地建写作业 + 向对端发 FileTransferSendRequest
                    const int32_t id = engine->ReceiveFiles(
                        norm.toStdString(), false, local_to.toStdString());
                    emit self->SigJobAdded(id, name, true);
                    // 目录下载:向远端要空目录清单,回包后在本地落地
                    // (对文件查询远端会返回空清单,无副作用)
                    engine->ReadEmptyDirs(norm.toStdString(), false);
                    self->pending_empty_dirs_[norm.toStdString()] = local_to;
                }
            }));
    }

    void FtCore::CancelJob(int32_t id) {
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        static_cast<void>(session->Post(
            "ft-cancel-job", [id](const auto& engine) { engine->CancelJob(id); }));
    }

    void FtCore::ConfirmOverwrite(int32_t job_id, int32_t file_num, int choice,
                                  uint64_t offset, bool apply_to_all) {
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        static_cast<void>(session->Post(
            "ft-confirm-overwrite",
            [job_id, file_num, choice, offset, apply_to_all](const auto& engine) {
                if (apply_to_all) {
                    // "应用到全部冲突":skip=false / overwrite|resume=true(io_loop.rs:673)
                    engine->SetOverwriteStrategy(job_id, choice != 0);
                }
                if (choice == 0) {
                    engine->ConfirmFile(job_id, file_num, false, 0);
                } else if (choice == 2) {
                    engine->ConfirmFile(job_id, file_num, true, offset);
                } else {
                    engine->ConfirmFile(job_id, file_num, true, 0);
                }
            }));
    }

    void FtCore::SetRateLimitBytesPerSec(uint64_t bps) {
        const auto session = session_.load();
        if (!accepting_.load() || !session) return;
        static_cast<void>(session->Post(
            "ft-rate-limit", [bps](const auto& engine) {
                engine->SetRateLimitBytesPerSec(bps);
            }));
    }

    void FtCore::ProcessMessage(const std::shared_ptr<px::ft::FtEngine>& engine,
                                const std::shared_ptr<Message>& msg) {
        if (msg->type() == MessageType::kFileAction) {
            // 对称语义:对端也可能发起(io_loop.rs 双端同构)
            engine->HandleFileAction(msg->file_action());
        } else if (msg->type() == MessageType::kFileResponse) {
            engine->HandleFileResponse(msg->file_response());
        }
    }

    void FtCore::ProcessResponse(const px::FileResponse& resp) {
        // 仅 FtAsyncSession state strand（引擎 response callback 触发）。
        using U = px::FileResponse::UnionCase;
        switch (resp.union_case()) {
            case U::kDir: {
                const auto& dir = resp.dir();
                if (dir.id() != 0) {
                    // 作业语境的文件列表(ReadAllFiles/下载握手),引擎已消费,UI 不展示
                    return;
                }
                LOGI("ft remote directory response: path {}, entries {}",
                     dir.path(), dir.entries_size());
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

    FtEntryList FtCore::ConvertEntries(const px::FileDirectory& dir) {
        FtEntryList out;
        out.reserve(dir.entries().size());
        for (const auto& e : dir.entries()) {
            FtEntryInfo info;
            info.name_ = QString::fromStdString(e.name());
            info.is_dir_ = e.entry_type() == px::FileType::Dir;
            info.is_drive_ = e.entry_type() == px::FileType::DirDrive;
            info.size_ = e.size();
            info.modified_time_ = (int64_t)e.modified_time();
            if (!e.abs_path().empty()) {
                info.abs_path_ = QString::fromStdString(e.abs_path());
            }
            out.push_back(info);
        }
        return out;
    }

    void FtCore::CreateRemoteEmptyDirs(
                                       const std::shared_ptr<px::ft::FtEngine>& engine,
                                       int32_t job_id, const QString& local_path,
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
            engine->CreateDir(job_id, target.toStdString());
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
