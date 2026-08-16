//
// ft 文件列表面板(本地/远程共用)
//

#include "ft_file_panel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>

#include "translator/px_translator.h"
#include "px_common_new/log.h"

namespace px
{

    static const char* kMimeLocalPaths = "application/x-px-ft-local";
    static const char* kMimeRemotePaths = "application/x-px-ft-remote";

    static QString FormatSize(uint64_t size) {
        if (size < 1024) return QString::number(size) + " B";
        if (size < 1024ull * 1024) return QString::number(size / 1024.0, 'f', 1) + " KB";
        if (size < 1024ull * 1024 * 1024) return QString::number(size / 1024.0 / 1024, 'f', 1) + " MB";
        return QString::number(size / 1024.0 / 1024 / 1024, 'f', 2) + " GB";
    }

    static QString FormatTime(int64_t secs) {
        if (secs <= 0) return {};
        return QDateTime::fromSecsSinceEpoch(secs).toString("yyyy-MM-dd HH:mm");
    }

    // ---------------- FtFileTable ----------------

    FtFileTable::FtFileTable(bool is_local, QWidget* parent)
        : QTableWidget(parent), is_local_(is_local) {
        setColumnCount(3);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        verticalHeader()->hide();
        verticalHeader()->setDefaultSectionSize(28);
        horizontalHeader()->setStretchLastSection(true);
        horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        setShowGrid(false);
        setAlternatingRowColors(true);
        setDragEnabled(true);
        setDragDropMode(QAbstractItemView::DragOnly);
        setContextMenuPolicy(Qt::CustomContextMenu);
    }

    QMimeData* FtFileTable::mimeData(const QList<QTableWidgetItem*>& items) const {
        QStringList paths;
        QSet<int> rows;
        for (const auto* item : items) {
            if (rows.contains(item->row())) continue;
            rows.insert(item->row());
            const auto p = item->data(Qt::UserRole).toString();
            if (!p.isEmpty()) paths.push_back(p);
        }
        if (paths.isEmpty()) return nullptr;
        auto* mime = new QMimeData();
        mime->setData(is_local_ ? kMimeLocalPaths : kMimeRemotePaths,
                      paths.join('\n').toUtf8());
        return mime;
    }

    // ---------------- FtFilePanel ----------------

    FtFilePanel::FtFilePanel(FtCore* core, bool is_local, QWidget* parent)
        : QWidget(parent), core_(core), is_local_(is_local) {
        setAcceptDrops(true);
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        // 标题
        auto* title = new QLabel(tcTr(is_local_ ? "id_file_trans_local_device"
                                                : "id_file_trans_remote_device"), this);
        title->setStyleSheet("font-weight: bold; font-size: 14px;");
        root->addWidget(title);

        // 工具条:Home / 上级 / 刷新 / 路径编辑 / 新建文件夹 / 上传|下载
        auto* bar = new QHBoxLayout();
        bar->setSpacing(4);
        auto make_btn = [this](const QString& text, const QString& tip) {
            auto* b = new QPushButton(text, this);
            b->setToolTip(tip);
            b->setFixedHeight(26);
            return b;
        };
        auto* home_btn = make_btn(QString::fromUtf8("⌂"), tcTr("id_file_trans_index"));
        auto* up_btn = make_btn(QString::fromUtf8("↑"), tcTr("id_file_trans_parent_directory"));
        auto* refresh_btn = make_btn(QString::fromUtf8("⟳"), tcTr("id_file_trans_refresh"));
        path_edit_ = new QLineEdit(this);
        path_edit_->setFixedHeight(26);
        auto* new_folder_btn = make_btn("+", tcTr("id_file_trans_new_folder"));
        transfer_btn_ = make_btn(tcTr(is_local_ ? "id_file_trans_upload" : "id_file_trans_down"), "");
        transfer_btn_->setStyleSheet("QPushButton { background: #2979ff; color: white; border: none; border-radius: 3px; padding: 0 10px; }"
                                     "QPushButton:hover { background: #448aff; }");

        bar->addWidget(home_btn);
        bar->addWidget(up_btn);
        bar->addWidget(refresh_btn);
        bar->addWidget(path_edit_, 1);
        bar->addWidget(new_folder_btn);
        bar->addWidget(transfer_btn_);
        root->addLayout(bar);

        // 文件表
        table_ = new FtFileTable(is_local_, this);
        table_->setHorizontalHeaderLabels({tcTr("id_file_trans_name"),
                                           tcTr("id_file_trans_size"),
                                           tcTr("id_file_trans_modify_time")});
        root->addWidget(table_, 1);

        connect(home_btn, &QPushButton::clicked, this, &FtFilePanel::NavigateHome);
        connect(up_btn, &QPushButton::clicked, this, [this]() {
            NavigateTo(ParentDirOf(current_dir_));
        });
        connect(refresh_btn, &QPushButton::clicked, this, &FtFilePanel::Refresh);
        connect(new_folder_btn, &QPushButton::clicked, this, &FtFilePanel::DoNewFolder);
        connect(transfer_btn_, &QPushButton::clicked, this, &FtFilePanel::DoTransferSelected);
        connect(path_edit_, &QLineEdit::returnPressed, this, [this]() {
            NavigateTo(path_edit_->text().trimmed());
        });
        connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            OnDoubleClick(row);
        });
        connect(table_, &QTableWidget::customContextMenuRequested, this, &FtFilePanel::OnContextMenu);
    }

    void FtFilePanel::NavigateHome() {
        if (is_local_) {
            NavigateTo(QDir::homePath());
        } else {
            NavigateTo("/");
        }
    }

    void FtFilePanel::Refresh() {
        if (is_local_) {
            RefreshLocal();
        } else {
            NavigateTo(current_dir_.isEmpty() ? "/" : current_dir_);
        }
    }

    void FtFilePanel::NavigateTo(const QString& path) {
        if (is_local_) {
            QString p = QDir::fromNativeSeparators(path);
            if (p == "/" || p == "\\") {
                p.clear(); // "" = 盘符列表
            }
            if (!p.isEmpty() && !QDir(p).exists()) {
                LOGW("ft local dir not exists: {}", p.toStdString());
                return;
            }
            current_dir_ = p;
            RefreshLocal();
        } else {
            QString p = path;
            p.replace('\\', '/');
            if (p.isEmpty()) p = "/";
            core_->ReadDir(p);
        }
    }

    void FtFilePanel::RefreshLocal() {
        table_->setRowCount(0);
        path_edit_->setText(current_dir_.isEmpty() ? tcTr("id_file_trans_disk") : current_dir_);

        if (current_dir_.isEmpty()) {
            // 盘符列表(对应 rustdesk Windows "/" 语义)
            const auto drives = QDir::drives();
            table_->setRowCount((int)drives.size());
            int row = 0;
            for (const auto& d : drives) {
                const QString name = d.path(); // "C:/"
                auto* item = new QTableWidgetItem(name);
                item->setData(Qt::UserRole, name);
                table_->setItem(row, 0, item);
                table_->setItem(row, 1, new QTableWidgetItem(""));
                table_->setItem(row, 2, new QTableWidgetItem(""));
                ++row;
            }
            return;
        }

        QDir dir(current_dir_);
        dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        dir.setSorting(QDir::DirsFirst | QDir::Name | QDir::IgnoreCase);
        const auto entries = dir.entryInfoList();
        table_->setRowCount((int)entries.size());
        int row = 0;
        for (const auto& fi : entries) {
            auto* item = new QTableWidgetItem(fi.fileName());
            item->setData(Qt::UserRole, fi.absoluteFilePath());
            item->setData(Qt::UserRole + 1, fi.isDir());
            table_->setItem(row, 0, item);
            auto* size_item = new QTableWidgetItem(fi.isDir() ? "" : FormatSize((uint64_t)fi.size()));
            size_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(row, 1, size_item);
            table_->setItem(row, 2, new QTableWidgetItem(fi.lastModified().toString("yyyy-MM-dd HH:mm")));
            ++row;
        }
    }

    void FtFilePanel::ShowDir(const QString& path, const QVector<FtEntryInfo>& entries) {
        if (is_local_) return;
        current_dir_ = path;
        path_edit_->setText(path);
        table_->setRowCount(0);
        table_->setRowCount((int)entries.size());
        int row = 0;
        for (const auto& e : entries) {
            auto* item = new QTableWidgetItem(e.name_);
            item->setData(Qt::UserRole, EntryFullPath(e.name_));
            item->setData(Qt::UserRole + 1, e.is_dir_ || e.is_drive_);
            table_->setItem(row, 0, item);
            auto* size_item = new QTableWidgetItem(
                (e.is_dir_ || e.is_drive_) ? "" : FormatSize(e.size_));
            size_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(row, 1, size_item);
            table_->setItem(row, 2, new QTableWidgetItem(FormatTime(e.modified_time_)));
            ++row;
        }
    }

    QString FtFilePanel::EntryFullPath(const QString& name) const {
        if (is_local_) {
            return current_dir_.isEmpty() ? name : QDir(current_dir_).filePath(name);
        }
        // 远程:盘符 "C:" 项拼成 "C:/"
        if (current_dir_ == "/" || current_dir_.isEmpty()) {
            return name.endsWith(':') ? name + "/" : name;
        }
        QString d = current_dir_;
        while (d.size() > 1 && d.endsWith('/')) d.chop(1);
        return d + "/" + name;
    }

    QString FtFilePanel::ParentDirOf(const QString& path) const {
        if (is_local_) {
            if (current_dir_.isEmpty()) return {};
            QDir d(current_dir_);
            if (!d.cdUp()) return {}; // 盘符根 -> 盘符列表
            const QString p = d.absolutePath();
            // "C:/" 再往上就是盘符列表
            if (p.size() <= 3 && p.endsWith(":/")) return {};
            return p;
        }
        QString p = path;
        while (p.size() > 1 && p.endsWith('/')) p.chop(1);
        if (p == "/" || p.isEmpty()) return "/";
        // "C:" 的上级是 "/"
        if (p.size() == 2 && p.endsWith(':')) return "/";
        const int idx = p.lastIndexOf('/');
        if (idx < 0) return "/";
        QString parent = p.left(idx);
        if (parent.size() == 2 && parent.endsWith(':')) parent += "/";
        return parent.isEmpty() ? "/" : parent;
    }

    QStringList FtFilePanel::SelectedPaths() const {
        QStringList paths;
        const auto items = table_->selectedItems();
        QSet<int> rows;
        for (const auto* item : items) {
            if (rows.contains(item->row())) continue;
            rows.insert(item->row());
            const auto p = item->data(Qt::UserRole).toString();
            if (!p.isEmpty()) paths.push_back(p);
        }
        return paths;
    }

    void FtFilePanel::OnDoubleClick(int row) {
        auto* item = table_->item(row, 0);
        if (!item) return;
        if (!item->data(Qt::UserRole + 1).toBool()) {
            return; // 文件双击不动作
        }
        NavigateTo(item->data(Qt::UserRole).toString());
    }

    void FtFilePanel::OnContextMenu(const QPoint& pos) {
        auto* item = table_->itemAt(pos);
        QMenu menu(this);
        auto* act_new = menu.addAction(tcTr("id_file_trans_new_folder"));
        QAction* act_rename = nullptr;
        QAction* act_del = nullptr;
        QAction* act_transfer = nullptr;
        if (item) {
            act_rename = menu.addAction(tcTr("id_file_trans_rename"));
            act_del = menu.addAction(tcTr("id_file_trans_del"));
            act_transfer = menu.addAction(tcTr(is_local_ ? "id_file_trans_upload" : "id_file_trans_down"));
        }
        menu.addSeparator();
        auto* act_refresh = menu.addAction(tcTr("id_file_trans_refresh"));
        auto* chosen = menu.exec(table_->viewport()->mapToGlobal(pos));
        if (!chosen) return;
        if (chosen == act_new) DoNewFolder();
        else if (chosen == act_rename) DoRename();
        else if (chosen == act_del) DoDelete();
        else if (chosen == act_transfer) DoTransferSelected();
        else if (chosen == act_refresh) Refresh();
    }

    void FtFilePanel::DoNewFolder() {
        bool ok = false;
        const QString name = QInputDialog::getText(this, tcTr("id_file_trans_new_folder"),
                                                   tcTr("id_file_trans_name"), QLineEdit::Normal,
                                                   "", &ok);
        if (!ok || name.trimmed().isEmpty()) return;
        if (name.contains('/') || name.contains('\\')) return;
        if (is_local_) {
            if (current_dir_.isEmpty()) return;
            if (!QDir(current_dir_).mkdir(name)) {
                QMessageBox::warning(this, tcTr("id_file_trans_new_folder"),
                                     tcTr("id_file_trans_new_folder_failed"));
            }
            RefreshLocal();
        } else {
            if (current_dir_ == "/" || current_dir_.isEmpty()) return;
            core_->CreateDir(EntryFullPath(name));
        }
    }

    void FtFilePanel::DoRename() {
        const auto paths = SelectedPaths();
        if (paths.size() != 1) return;
        const QString old_path = paths.first();
        const QString old_name = QDir(old_path).dirName().isEmpty()
            ? old_path : QFileInfo(old_path).fileName();
        bool ok = false;
        const QString new_name = QInputDialog::getText(this, tcTr("id_file_trans_rename"),
                                                       tcTr("id_file_trans_name"), QLineEdit::Normal,
                                                       old_name, &ok);
        if (!ok || new_name.trimmed().isEmpty() || new_name == old_name) return;
        if (new_name.contains('/') || new_name.contains('\\')) return;
        if (is_local_) {
            if (!QDir().rename(old_path, QFileInfo(old_path).absolutePath() + "/" + new_name)) {
                QMessageBox::warning(this, tcTr("id_file_trans_rename"),
                                     tcTr("id_file_trans_rename_failed"));
            }
            RefreshLocal();
        } else {
            core_->RenameEntry(old_path, new_name);
        }
    }

    void FtFilePanel::DoDelete() {
        const auto paths = SelectedPaths();
        if (paths.isEmpty()) return;
        const auto ret = QMessageBox::question(this, tcTr("id_file_trans_del"),
                                               tcTr("id_file_trans_del") + QString(" (%1)").arg(paths.size()));
        if (ret != QMessageBox::Yes) return;
        for (const auto& p : paths) {
            if (is_local_) {
                QFileInfo fi(p);
                bool ok_del = fi.isDir() ? QDir(p).removeRecursively() : QFile::remove(p);
                if (!ok_del) {
                    LOGW("ft local remove failed: {}", p.toStdString());
                }
            } else {
                // 远程:目录递归删除(引擎 RemoveDir recursive=true)
                bool is_dir = false;
                for (int r = 0; r < table_->rowCount(); ++r) {
                    auto* it = table_->item(r, 0);
                    if (it && it->data(Qt::UserRole).toString() == p) {
                        is_dir = it->data(Qt::UserRole + 1).toBool();
                        break;
                    }
                }
                core_->RemoveEntry(p, is_dir);
            }
        }
        if (is_local_) RefreshLocal();
        // 远程栏靠 SigDirOpDone 触发刷新(ft_window 接线)
    }

    void FtFilePanel::DoTransferSelected() {
        const auto paths = SelectedPaths();
        if (paths.isEmpty()) return;
        if (is_local_) {
            emit SigUploadRequested(paths);
        } else {
            emit SigDownloadRequested(paths);
        }
    }

    void FtFilePanel::dragEnterEvent(QDragEnterEvent* event) {
        const auto* mime = event->mimeData();
        if (is_local_) {
            // 本地栏:接受远程栏拖入(下载)
            if (mime->hasFormat(kMimeRemotePaths)) {
                event->acceptProposedAction();
            }
        } else {
            // 远程栏:接受本地栏拖入与 OS 拖入(上传)
            if (mime->hasFormat(kMimeLocalPaths) || mime->hasUrls()) {
                event->acceptProposedAction();
            }
        }
    }

    void FtFilePanel::dropEvent(QDropEvent* event) {
        const auto* mime = event->mimeData();
        if (is_local_) {
            if (mime->hasFormat(kMimeRemotePaths)) {
                const auto paths = QString::fromUtf8(mime->data(kMimeRemotePaths)).split('\n', Qt::SkipEmptyParts);
                if (!paths.isEmpty()) emit SigDownloadRequested(paths);
                event->acceptProposedAction();
            }
        } else {
            if (mime->hasFormat(kMimeLocalPaths)) {
                const auto paths = QString::fromUtf8(mime->data(kMimeLocalPaths)).split('\n', Qt::SkipEmptyParts);
                if (!paths.isEmpty()) emit SigUploadRequested(paths);
                event->acceptProposedAction();
            } else if (mime->hasUrls()) {
                QStringList paths;
                for (const auto& url : mime->urls()) {
                    if (url.isLocalFile()) paths.push_back(url.toLocalFile());
                }
                if (!paths.isEmpty()) emit SigUploadRequested(paths);
                event->acceptProposedAction();
            }
        }
    }

}
