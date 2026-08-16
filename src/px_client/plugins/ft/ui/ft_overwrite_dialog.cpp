//
// ft 覆盖确认弹框
//

#include "ft_overwrite_dialog.h"

#include <QCheckBox>
#include <QDateTime>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "translator/px_translator.h"

namespace px
{

    static QString FormatSize(qint64 size) {
        if (size < 1024) return QString::number(size) + " B";
        if (size < 1024 * 1024) return QString::number(size / 1024.0, 'f', 1) + " KB";
        if (size < 1024ll * 1024 * 1024) return QString::number(size / 1024.0 / 1024, 'f', 1) + " MB";
        return QString::number(size / 1024.0 / 1024 / 1024, 'f', 2) + " GB";
    }

    FtOverwriteDialog::FtOverwriteDialog(const QString& path, bool is_upload, bool is_identical,
                                         QWidget* parent)
        : QDialog(parent) {
        setModal(true);
        setMinimumWidth(460);
        setWindowTitle(tcTr(is_upload ? "id_file_trans_upload_prompt" : "id_file_trans_down_tips"));

        const QFileInfo local_fi(path);
        // 续传凭证:接收侧落 <path>.download(rustdesk fs.rs:760 语义)。
        // 上传方向的远端 partial 由引擎 identical+resume 分支自动续传,不走弹框。
        const QFileInfo partial_fi(path + ".download");
        const uint64_t partial_size =
            (!is_upload && partial_fi.exists()) ? (uint64_t)partial_fi.size() : 0;

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 16, 16, 16);
        root->setSpacing(10);

        auto* msg = new QLabel(tcTr("id_file_trans_file_already_exist"), this);
        msg->setStyleSheet("font-weight: bold; font-size: 14px;");
        root->addWidget(msg);

        auto* file_lab = new QLabel(
            QString("%1: %2").arg(tcTr("id_file_trans_file_name"), local_fi.fileName()), this);
        file_lab->setWordWrap(true);
        root->addWidget(file_lab);

        if (local_fi.exists()) {
            auto* info_lab = new QLabel(
                QString("%1    %2").arg(FormatSize(local_fi.size()))
                                   .arg(local_fi.lastModified().toString("yyyy-MM-dd HH:mm")),
                this);
            info_lab->setStyleSheet("color: #666;");
            root->addWidget(info_lab);
        }
        if (partial_size > 0) {
            auto* partial_lab = new QLabel(
                QString("%1: %2").arg(tcTr("id_file_trans_progress"), FormatSize((qint64)partial_size)),
                this);
            partial_lab->setStyleSheet("color: #666;");
            root->addWidget(partial_lab);
        }

        auto* apply_all = new QCheckBox(tcTr("id_file_trans_apply_to_all"), this);
        root->addWidget(apply_all);

        auto* btn_bar = new QHBoxLayout();
        btn_bar->addStretch();
        auto* skip_btn = new QPushButton(tcTr("id_file_trans_skip"), this);
        auto* overwrite_btn = new QPushButton(tcTr("id_file_trans_cover"), this);
        auto* resume_btn = new QPushButton(tcTr("id_file_trans_resume"), this);
        skip_btn->setFixedSize(90, 30);
        overwrite_btn->setFixedSize(90, 30);
        resume_btn->setFixedSize(90, 30);
        overwrite_btn->setStyleSheet("QPushButton { background: #2979ff; color: white; border: none; border-radius: 3px; }"
                                     "QPushButton:hover { background: #448aff; }");
        btn_bar->addWidget(skip_btn);
        btn_bar->addWidget(overwrite_btn);
        btn_bar->addWidget(resume_btn);
        root->addLayout(btn_bar);

        // 续传按钮:本端存在 partial(.download)时可用;is_identical 时跳过是更合理默认
        resume_btn->setVisible(partial_size > 0);

        connect(skip_btn, &QPushButton::clicked, this, [this, apply_all]() {
            decision_ = {0, 0, apply_all->isChecked()};
            accept();
        });
        connect(overwrite_btn, &QPushButton::clicked, this, [this, apply_all]() {
            decision_ = {1, 0, apply_all->isChecked()};
            accept();
        });
        connect(resume_btn, &QPushButton::clicked, this, [this, apply_all, partial_size]() {
            decision_ = {2, partial_size, apply_all->isChecked()};
            accept();
        });
    }

}
