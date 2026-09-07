//
// ft 三栏文件管理窗口
//

#include "ft_window.h"
#include "ft_file_panel.h"
#include "ft_transfer_queue.h"
#include "ft_overwrite_dialog.h"

#include <QSplitter>
#include <QVBoxLayout>
#include <QHash>
#include <QMetaObject>
#include <QPointer>

#include "px_common/log.h"
#include "translator/px_translator.h"

namespace px
{

    FtWindow::FtWindow(std::shared_ptr<FtCore> core, QWidget* parent)  // NOLINT(gammaray-raw-pointer-boundary): Qt parent ownership API
        : QWidget(parent), core_(std::move(core)) {
        // 窗口灰底 + 白色圆角卡片(左右文件栏 / 底部传输条)
        setObjectName("ftRoot");
        setAttribute(Qt::WA_StyledBackground, true); // 裸 QWidget 子类需显式开启,样式表背景才会自绘
        setStyleSheet("#ftRoot { background: #f0f2f5; }"
                      "#ftCard { background: #ffffff; border-radius: 10px; }"
                      "QSplitter::handle { background: transparent; }");
        auto* root = new QVBoxLayout(this);
        root_layout_ = root;
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(8);

        // 上:本地 | 远程(圆角卡片,中间仅 8px 缝,不可上下拖动)
        auto* pane_split = new QSplitter(Qt::Horizontal, this);
        pane_split_ = pane_split;
        pane_split->setHandleWidth(8);
        local_panel_ = new FtFilePanel(core_, true, pane_split);
        remote_panel_ = new FtFilePanel(core_, false, pane_split);
        local_panel_->setObjectName("ftCard");
        remote_panel_->setObjectName("ftCard");
        pane_split->addWidget(local_panel_);
        pane_split->addWidget(remote_panel_);
        pane_split->setStretchFactor(0, 1);
        pane_split->setStretchFactor(1, 1);

        // 下:传输队列(圆角卡片;展开后占满整个窗口)
        queue_ = new FtTransferQueue(core_, this);
        queue_->setObjectName("ftCard");

        root->addWidget(pane_split, 1);
        root->addWidget(queue_, 0);
        connect(queue_.get(), &FtTransferQueue::SigExpandedToggled, this,
                &FtWindow::OnExpandedToggled);

        // ---------------- 传输请求接线 ----------------
        connect(local_panel_.get(), &FtFilePanel::SigUploadRequested, this,
                &FtWindow::OnUploadRequested);
        connect(remote_panel_.get(), &FtFilePanel::SigDownloadRequested, this,
                &FtWindow::OnDownloadRequested);

        // ---------------- core 信号 ----------------
        const QPointer<FtWindow> guarded_self(this);
        connect(core_.get(), &FtCore::SigRemoteDir, this,
                [guarded_self](
                    const QString& path, const FtEntryList& entries) {
                    if (!guarded_self) {
                        return;
                    }
                    QMetaObject::invokeMethod(
                        guarded_self.data(),
                        [guarded_self, path, entries]() {
                            if (guarded_self) {
                                guarded_self->OnRemoteDir(path, entries);
                            }
                        },
                        Qt::QueuedConnection);
                },
                Qt::DirectConnection);
        connect(core_.get(), &FtCore::SigJobAdded, this,
                &FtWindow::OnJobAdded);
        connect(core_.get(), &FtCore::SigJobProgress, this,
                [guarded_self](const FtJobStatusInfo& status) {
                    if (!guarded_self) {
                        return;
                    }
                    QMetaObject::invokeMethod(
                        guarded_self.data(),
                        [guarded_self, status]() {
                            if (guarded_self) {
                                guarded_self->OnJobProgress(status);
                            }
                        },
                        Qt::QueuedConnection);
                },
                Qt::DirectConnection);
        connect(core_.get(), &FtCore::SigJobDone, this,
                &FtWindow::OnJobDone);
        connect(core_.get(), &FtCore::SigOverwriteConfirm, this,
                &FtWindow::OnOverwriteConfirm);
        // 目录操作回执:刷新远程栏(失败提示经日志,错误详情已由对端 error 携带)
        connect(core_.get(), &FtCore::SigDirOpDone, this,
                &FtWindow::OnDirOpDone);
    }

    void FtWindow::OnExpandedToggled(bool expanded) {
        if (pane_split_) {
            pane_split_->setVisible(!expanded);
        }
        if (root_layout_) {
            root_layout_->setStretch(1, expanded ? 1 : 0);
        }
    }

    void FtWindow::OnUploadRequested(const QStringList& paths) {
        if (!remote_panel_ || !core_) {
            return;
        }
        const QString remote_dir = remote_panel_->CurrentDir();
        if (remote_dir.isEmpty() || remote_dir == "/") {
            LOGW("ft upload rejected: remote dir is drive list");
            return;
        }
        core_->StartUpload(paths, remote_dir);
    }

    void FtWindow::OnDownloadRequested(const QStringList& paths) {
        if (!local_panel_ || !core_) {
            return;
        }
        const QString local_dir = local_panel_->CurrentDir();
        if (local_dir.isEmpty()) {
            LOGW("ft download rejected: local dir is drive list");
            return;
        }
        core_->StartDownload(paths, local_dir);
    }

    void FtWindow::OnRemoteDir(
        const QString& path, const FtEntryList& entries) {
        if (remote_panel_) {
            remote_panel_->ShowDir(path, entries);
        }
    }

    void FtWindow::OnJobAdded(
        int id, const QString& name, bool is_download) {
        job_download_[id] = is_download;
        if (queue_) {
            queue_->AddJob(id, name, is_download);
        }
    }

    void FtWindow::OnJobProgress(const FtJobStatusInfo& status) {
        if (queue_) {
            queue_->UpdateJob(status);
        }
    }

    void FtWindow::OnJobDone(int id, const QString& error) {
        if (queue_) {
            queue_->FinishJob(id, error);
        }
        const bool is_download = job_download_.take(id);
        if (!error.isEmpty()) {
            return;
        }
        if (is_download && local_panel_) {
            local_panel_->Refresh();
        }
        else if (!is_download && remote_panel_) {
            remote_panel_->Refresh();
        }
    }

    void FtWindow::OnOverwriteConfirm(
        int job_id, int file_num, const QString& path,
        bool is_upload, bool is_identical) {
        pending_confirms_.push_back(
            {job_id, file_num, path, is_upload, is_identical});
        ShowNextOverwriteConfirm();
    }

    void FtWindow::OnDirOpDone(int operation_id, const QString& error) {
        (void)operation_id;
        if (!error.isEmpty()) {
            LOGW("ft dir op failed: {}", error.toStdString());
        }
        if (remote_panel_) {
            remote_panel_->Refresh();
        }
    }

    void FtWindow::SetRemoteDeviceName(const QString& name) {
        if (remote_panel_) {
            remote_panel_->SetDeviceName(name);
        }
    }

    void FtWindow::OnShow() {
        if (first_show_) {
            first_show_ = false;
            local_panel_->NavigateHome();
            remote_panel_->NavigateHome(); // "/" 列远端盘符
        } else {
            remote_panel_->Refresh();
        }
    }

    void FtWindow::OnTransportConnected() {
        if (!first_show_ && remote_panel_) {
            remote_panel_->Refresh();
        }
    }

    void FtWindow::ShowNextOverwriteConfirm() {
        if (confirm_showing_ || pending_confirms_.empty()) {
            return;
        }
        confirm_showing_ = true;
        const auto pc = pending_confirms_.front();
        pending_confirms_.pop_front();

        // 弹框期间引擎作业在 worker 线程等待,其他作业照常推进
        FtOverwriteDialog dlg(pc.path, pc.is_upload, pc.is_identical, this);
        dlg.exec();
        const auto d = dlg.Decision();
        core_->ConfirmOverwrite(pc.job_id, pc.file_num, d.choice, d.offset, d.apply_to_all);

        confirm_showing_ = false;
        ShowNextOverwriteConfirm();
    }

}
