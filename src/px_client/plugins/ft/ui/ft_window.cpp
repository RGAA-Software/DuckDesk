//
// ft 三栏文件管理窗口
//

#include "ft_window.h"
#include "ft_file_panel.h"
#include "ft_transfer_queue.h"
#include "ft_overwrite_dialog.h"

#include <QSplitter>
#include <QVBoxLayout>

#include "px_common_new/log.h"
#include "translator/px_translator.h"

namespace px
{

    FtWindow::FtWindow(FtCore* core, QWidget* parent)
        : QWidget(parent), core_(core) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        auto* main_split = new QSplitter(Qt::Vertical, this);

        // 上:本地 | 远程
        auto* pane_split = new QSplitter(Qt::Horizontal, main_split);
        local_panel_ = new FtFilePanel(core_, true, pane_split);
        remote_panel_ = new FtFilePanel(core_, false, pane_split);
        pane_split->addWidget(local_panel_);
        pane_split->addWidget(remote_panel_);
        pane_split->setStretchFactor(0, 1);
        pane_split->setStretchFactor(1, 1);

        // 下:传输队列
        queue_ = new FtTransferQueue(core_, main_split);

        main_split->addWidget(pane_split);
        main_split->addWidget(queue_);
        main_split->setStretchFactor(0, 3);
        main_split->setStretchFactor(1, 1);
        root->addWidget(main_split);

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
        connect(core_, &FtCore::SigJobAdded, queue_, &FtTransferQueue::AddJob);
        connect(core_, &FtCore::SigJobProgress, queue_, &FtTransferQueue::UpdateJob);
        connect(core_, &FtCore::SigJobDone, this, [this](int id, const QString& err) {
            queue_->FinishJob(id, err);
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

    void FtWindow::OnShow() {
        if (first_show_) {
            first_show_ = false;
            local_panel_->NavigateHome();
            remote_panel_->NavigateHome(); // "/" 列远端盘符
        } else {
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
