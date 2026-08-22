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

#include "px_common_new/log.h"
#include "translator/px_translator.h"

namespace px
{

    FtWindow::FtWindow(FtCore* core, QWidget* parent)
        : QWidget(parent), core_(core) {
        // 窗口灰底 + 白色圆角卡片(左右文件栏 / 底部传输条)
        setObjectName("ftRoot");
        setAttribute(Qt::WA_StyledBackground, true); // 裸 QWidget 子类需显式开启,样式表背景才会自绘
        setStyleSheet("#ftRoot { background: #f0f2f5; }"
                      "#ftCard { background: #ffffff; border-radius: 10px; }"
                      "QSplitter::handle { background: transparent; }");
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 8, 8, 8);
        root->setSpacing(8);

        // 上:本地 | 远程(圆角卡片,中间仅 8px 缝,不可上下拖动)
        auto* pane_split = new QSplitter(Qt::Horizontal, this);
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
        connect(queue_, &FtTransferQueue::SigExpandedToggled, this,
                [this, root, pane_split](bool expanded) {
            pane_split->setVisible(!expanded);
            root->setStretch(1, expanded ? 1 : 0); // 展开后队列占满窗口
        });

        // ---------------- 传输请求接线 ----------------
        connect(local_panel_, &FtFilePanel::SigUploadRequested, this, [this](const QStringList& paths) {
            const QString remote_dir = remote_panel_->CurrentDir();
            if (remote_dir.isEmpty() || remote_dir == "/") {
                LOGW("ft upload rejected: remote dir is drive list");
                return;
            }
            core_->StartUpload(paths, remote_dir);
        });
        connect(remote_panel_, &FtFilePanel::SigDownloadRequested, this, [this](const QStringList& paths) {
            const QString local_dir = local_panel_->CurrentDir();
            if (local_dir.isEmpty()) {
                LOGW("ft download rejected: local dir is drive list");
                return;
            }
            core_->StartDownload(paths, local_dir);
        });

        // ---------------- core 信号 ----------------
        connect(core_, &FtCore::SigRemoteDir, this,
                [this](const QString& path, const QVector<FtEntryInfo>& entries) {
            remote_panel_->ShowDir(path, entries);
        });
        connect(core_, &FtCore::SigJobAdded, this,
                [this](int id, const QString& name, bool is_download) {
            job_download_[id] = is_download;
            queue_->AddJob(id, name, is_download);
        });
        connect(core_, &FtCore::SigJobProgress, queue_, &FtTransferQueue::UpdateJob);
        connect(core_, &FtCore::SigJobDone, this, [this](int id, const QString& err) {
            queue_->FinishJob(id, err);
            // 传输结束自动刷新接收侧:上传->远程栏,下载->本地栏
            const bool is_download = job_download_.take(id);
            if (err.isEmpty()) {
                if (is_download) {
                    local_panel_->Refresh();
                } else {
                    remote_panel_->Refresh();
                }
            }
        });
        connect(core_, &FtCore::SigOverwriteConfirm, this,
                [this](int job_id, int file_num, const QString& path, bool is_upload, bool is_identical) {
            pending_confirms_.push_back({job_id, file_num, path, is_upload, is_identical});
            ShowNextOverwriteConfirm();
        });
        // 目录操作回执:刷新远程栏(失败提示经日志,错误详情已由对端 error 携带)
        connect(core_, &FtCore::SigDirOpDone, this, [this](int op_id, const QString& error_or_empty) {
            (void)op_id;
            if (!error_or_empty.isEmpty()) {
                LOGW("ft dir op failed: {}", error_or_empty.toStdString());
            }
            remote_panel_->Refresh();
        });
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
