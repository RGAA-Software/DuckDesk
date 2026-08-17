//
// ft 文件列表面板(本地/远程共用)
//

#include "ft_file_panel.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QSet>
#include <QStandardPaths>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>

#include <algorithm>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h> // SHGetFileInfoW / SHFILEINFOW / SHGFI_DISPLAYNAME
#pragma comment(lib, "shell32")
#endif

#include "translator/px_translator.h"
#include "px_common_new/log.h"

namespace px
{

    static const char* kMimeLocalPaths = "application/x-px-ft-local";
    static const char* kMimeRemotePaths = "application/x-px-ft-remote";

    // ---------------- 样式(沿用浅色,只定结构需要的部分) ----------------
    // 面板/工具条/面包屑
    static const char* kPanelStyle = R"(
        QPushButton { background: transparent; border: none; border-radius: 4px; }
        QPushButton:hover { background: #e3e9f3; }
        QPushButton:pressed { background: #d3ddea; }
    )";
    // 文件表(整行选中色强制不透明,避免全局 qss 发淡;表体透明,圆角卡片底由面板提供)
    static const char* kTableStyle = R"(
        QTableWidget { background: transparent; border: none; alternate-background-color: #f7f9fc; }
        QHeaderView::section { background: transparent; color: #666666; border: none;
                               border-bottom: 1px solid #e0e4ea; padding: 4px; }
        QTableWidget::item { border: none; padding-left: 4px; }
        QTableWidget::item:selected { background-color: #2979ff; color: #ffffff; }
        QTableWidget::item:selected:!active { background-color: #2979ff; color: #ffffff; }
        QTableWidget::item:hover { background: #e3e9f3; }
    )";

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

    // 类型列:磁盘/文件夹/小写扩展名(对齐参考 UI 的 zip/rar 风格)
    static QString EntryTypeString(bool is_dir, bool is_drive, const QString& name) {
        if (is_drive) return tcTr("id_file_trans_disk");
        if (is_dir) return tcTr("id_file_trans_folder");
        const QString suffix = QFileInfo(name).suffix().toLower();
        return suffix;
    }

#ifdef _WIN32
    // Shell 显示名(盘符 -> "本地磁盘 (C:)"/"NewDisk (D:)",常用文件夹 -> 本地化名称)
    static QString ShellDisplayName(const QString& path) {
        SHFILEINFOW sfi{};
        const auto native = QDir::toNativeSeparators(path);
        const DWORD_PTR ok = SHGetFileInfoW(
            reinterpret_cast<LPCWSTR>(native.utf16()), 0, &sfi, sizeof(sfi), SHGFI_DISPLAYNAME);
        return ok ? QString::fromWCharArray(sfi.szDisplayName) : QString();
    }
#else
    static QString ShellDisplayName(const QString&) { return {}; }
#endif

    // ---------------- FtFileTable ----------------

    FtFileTable::FtFileTable(bool is_local, QWidget* parent)
        : QTableWidget(parent), is_local_(is_local) {
        setColumnCount(4);
        setSelectionBehavior(QAbstractItemView::SelectRows);
        setSelectionMode(QAbstractItemView::ExtendedSelection);
        setEditTriggers(QAbstractItemView::NoEditTriggers);
        verticalHeader()->hide();
        verticalHeader()->setDefaultSectionSize(30);
        horizontalHeader()->setStretchLastSection(false);
        horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        setColumnWidth(1, 90);
        horizontalHeader()->setSectionResizeMode(2, QHeaderView::Fixed);
        setColumnWidth(2, 140);
        horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        setColumnWidth(3, 70);
        // 手动排序(目录恒排前),表头仅显示排序箭头
        horizontalHeader()->setSortIndicatorShown(true);
        horizontalHeader()->setSortIndicator(0, Qt::AscendingOrder);
        setShowGrid(false);
        setAlternatingRowColors(true);
        setStyleSheet(kTableStyle);
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
        setAttribute(Qt::WA_StyledBackground, true); // 圆角卡片背景(#ftCard)需要显式开启自绘
        setStyleSheet(kPanelStyle);
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 6, 6, 6);
        root->setSpacing(6);

        // 标题
        title_label_ = new QLabel(tcTr(is_local_ ? "id_file_trans_local_device"
                                                 : "id_file_trans_remote_device"), this);
        title_label_->setStyleSheet("font-weight: bold; font-size: 14px;");
        root->addWidget(title_label_);

        // 工具条:上级 / 刷新 / 新建文件夹 / 上传|下载(方向箭头,贴中间缝)
        // 图标:Google Material Symbols,脚本 scripts/download_material_icons.py 下载处理
        auto* bar = new QHBoxLayout();
        bar->setSpacing(4);
        auto make_btn = [this](const QString& icon, const QString& tip) {
            auto* b = new QPushButton(this);
            b->setIcon(QIcon(icon));
            b->setIconSize(QSize(20, 20));
            b->setToolTip(tip);
            b->setFixedSize(36, 32);
            return b;
        };
        auto* up_btn = make_btn(":/ft/icons/ic_arrow_upward.svg", tcTr("id_file_trans_parent_directory"));
        auto* refresh_btn = make_btn(":/ft/icons/ic_refresh.svg", tcTr("id_file_trans_refresh"));
        auto* new_folder_btn = make_btn(":/ft/icons/ic_create_new_folder.svg", tcTr("id_file_trans_new_folder"));
        // 方向按钮:上传 →(指向远端)、下载 ←(指向本机),箭头朝向即数据流向
        transfer_btn_ = make_btn(is_local_ ? ":/ft/icons/ic_arrow_forward.svg" : ":/ft/icons/ic_arrow_back.svg",
                                 tcTr(is_local_ ? "id_file_trans_upload" : "id_file_trans_down"));
        transfer_btn_->setFixedSize(40, 32);
        transfer_btn_->setStyleSheet("QPushButton { background: #2979ff; border: none; border-radius: 4px; }"
                                     "QPushButton:hover { background: #448aff; }");

        // 面包屑(路径段按钮,点击导航;根 = 此电脑)
        auto* crumb_wrap = new QWidget(this);
        crumb_bar_ = new QHBoxLayout(crumb_wrap);
        crumb_bar_->setContentsMargins(2, 0, 2, 0);
        crumb_bar_->setSpacing(0);

        if (is_local_) {
            // 本地栏:上传按钮贴右侧(靠近远端栏)
            bar->addWidget(up_btn);
            bar->addWidget(refresh_btn);
            bar->addWidget(crumb_wrap, 1);
            bar->addWidget(new_folder_btn);
            bar->addWidget(transfer_btn_);
        } else {
            // 远程栏:下载按钮贴左侧(靠近本地栏)
            bar->addWidget(transfer_btn_);
            bar->addWidget(up_btn);
            bar->addWidget(refresh_btn);
            bar->addWidget(crumb_wrap, 1);
            bar->addWidget(new_folder_btn);
        }
        root->addLayout(bar);

        // 文件表
        table_ = new FtFileTable(is_local_, this);
        table_->setHorizontalHeaderLabels({tcTr("id_file_trans_name"),
                                           tcTr("id_file_trans_size"),
                                           tcTr("id_file_trans_modify_time"),
                                           tcTr("id_file_trans_type")});
        root->addWidget(table_, 1);

        connect(up_btn, &QPushButton::clicked, this, [this]() {
            NavigateTo(ParentDirOf(current_dir_));
        });
        connect(refresh_btn, &QPushButton::clicked, this, &FtFilePanel::Refresh);
        connect(new_folder_btn, &QPushButton::clicked, this, &FtFilePanel::DoNewFolder);
        connect(transfer_btn_, &QPushButton::clicked, this, &FtFilePanel::DoTransferSelected);
        connect(table_, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
            OnDoubleClick(row);
        });
        connect(table_, &QTableWidget::customContextMenuRequested, this, &FtFilePanel::OnContextMenu);
        connect(table_->horizontalHeader(), &QHeaderView::sectionClicked, this, &FtFilePanel::OnSortRequested);
        RebuildBreadcrumb();
    }

    void FtFilePanel::SetDeviceName(const QString& name) {
        if (name.isEmpty()) return;
        title_label_->setText(QString("%1 (%2)").arg(
            tcTr(is_local_ ? "id_file_trans_local_device" : "id_file_trans_remote_device"), name));
    }

    void FtFilePanel::NavigateHome() {
        // 两侧一致:回到根视图("此电脑")
        NavigateTo(is_local_ ? QString() : QString("/"));
    }

    void FtFilePanel::OnSortRequested(int col) {
        if (col == sort_col_) {
            sort_order_ = (sort_order_ == Qt::AscendingOrder) ? Qt::DescendingOrder : Qt::AscendingOrder;
        } else {
            sort_col_ = col;
            sort_order_ = Qt::AscendingOrder;
        }
        table_->horizontalHeader()->setSortIndicator(sort_col_, sort_order_);
        if (is_local_) {
            RefreshLocal();
        } else {
            FillRemote();
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
                p.clear(); // "" = 根视图(此电脑)
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

    // 面包屑:此电脑 > D: > sub > ...;点击段导航
    void FtFilePanel::RebuildBreadcrumb() {
        QLayoutItem* item;
        while ((item = crumb_bar_->takeAt(0)) != nullptr) {
            if (item->widget()) item->widget()->deleteLater();
            delete item;
        }

        auto add_btn = [this](const QString& text, const QString& target) {
            auto* b = new QPushButton(text, crumb_bar_->parentWidget());
            b->setFlat(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setFixedHeight(28);
            b->setStyleSheet(
                "QPushButton { color: #333333; background: transparent; border: none;"
                " border-radius: 4px; padding: 0 6px; }"
                "QPushButton:hover { background: #e3e9f3; }");
            connect(b, &QPushButton::clicked, this, [this, target]() {
                NavigateTo(target);
            });
            crumb_bar_->addWidget(b);
        };
        auto add_sep = [this]() {
            auto* l = new QLabel(">", crumb_bar_->parentWidget());
            l->setStyleSheet("color: #999; padding: 0 2px;");
            crumb_bar_->addWidget(l);
        };

        add_btn(tcTr("id_file_trans_this_pc"), is_local_ ? QString() : QString("/"));

        QString p = current_dir_;
        if (!is_local_ && (p == "/" || p.isEmpty())) p.clear();
        if (!p.isEmpty()) {
            const auto segs = p.split('/', Qt::SkipEmptyParts); // ["D:", "foo", ...]
            QString prefix;
            for (int i = 0; i < segs.size(); ++i) {
                if (i == 0) {
                    prefix = segs[0].endsWith(':') ? segs[0] + "/" : segs[0];
                } else {
                    prefix += (prefix.endsWith('/') ? "" : "/") + segs[i];
                }
                add_sep();
                add_btn(segs[i], prefix);
            }
        }
        crumb_bar_->addStretch(1);
    }

    void FtFilePanel::RefreshLocal() {
        table_->setRowCount(0);
        RebuildBreadcrumb();

        if (current_dir_.isEmpty()) {
            FillLocalDrivesAndPinned();
            return;
        }

        QDir dir(current_dir_);
        dir.setFilter(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot);
        auto entries = dir.entryInfoList();
        // 手动排序:目录恒排前,再按选中列(名称/大小/修改时间/类型)
        const int col = sort_col_;
        const bool asc = (sort_order_ == Qt::AscendingOrder);
        std::sort(entries.begin(), entries.end(), [col, asc](const QFileInfo& a, const QFileInfo& b) {
            if (a.isDir() != b.isDir()) return a.isDir();
            int cmp = 0;
            if (col == 1) {
                cmp = (a.size() < b.size()) ? -1 : (a.size() > b.size() ? 1 : 0);
            } else if (col == 2) {
                cmp = (a.lastModified() < b.lastModified()) ? -1 : (a.lastModified() > b.lastModified() ? 1 : 0);
            } else if (col == 3) {
                cmp = QString::compare(EntryTypeString(false, false, a.fileName()),
                                       EntryTypeString(false, false, b.fileName()), Qt::CaseInsensitive);
            } else {
                cmp = QString::compare(a.fileName(), b.fileName(), Qt::CaseInsensitive);
            }
            return asc ? (cmp < 0) : (cmp > 0);
        });
        table_->setRowCount((int)entries.size());
        int row = 0;
        QFileIconProvider icon_provider;
        for (const auto& fi : entries) {
            auto* item = new QTableWidgetItem(icon_provider.icon(fi), fi.fileName());
            item->setData(Qt::UserRole, fi.absoluteFilePath());
            item->setData(Qt::UserRole + 1, fi.isDir());
            table_->setItem(row, 0, item);
            auto* size_item = new QTableWidgetItem(fi.isDir() ? "" : FormatSize((uint64_t)fi.size()));
            size_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(row, 1, size_item);
            table_->setItem(row, 2, new QTableWidgetItem(fi.lastModified().toString("yyyy-MM-dd HH:mm")));
            table_->setItem(row, 3, new QTableWidgetItem(EntryTypeString(fi.isDir(), false, fi.fileName())));
            ++row;
        }
    }

    // 本地根视图("此电脑"):盘符(带卷标)+ 常用文件夹,默认首屏即此视图
    void FtFilePanel::FillLocalDrivesAndPinned() {
        struct RootItem {
            QString display;   // 显示名(shell 本地化)
            QString full_path; // 导航路径
            bool is_drive = false;
            QIcon icon;
        };
        QList<RootItem> items;

        QFileIconProvider icon_provider;
        // 盘符( shell 显示名:"本地磁盘 (C:)" / "NewDisk (D:)")
        for (const auto& d : QDir::drives()) {
            RootItem it;
            it.display = ShellDisplayName(d.path());
            if (it.display.isEmpty()) it.display = d.path(); // 兜底 "C:/"
            it.full_path = d.path();
            it.is_drive = true;
            it.icon = icon_provider.icon(QFileIconProvider::Drive);
            items.push_back(it);
        }
        // 常用文件夹:用户目录 + 桌面/下载/文档/图片/音乐/视频
        QStringList pinned;
        pinned << QDir::homePath()
               << QStandardPaths::writableLocation(QStandardPaths::DesktopLocation)
               << QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
               << QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation)
               << QStandardPaths::writableLocation(QStandardPaths::PicturesLocation)
               << QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
               << QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
        QSet<QString> seen;
        for (const auto& raw : pinned) {
            if (raw.isEmpty()) continue;
            const QString p = QDir::fromNativeSeparators(raw);
            const QFileInfo fi(p);
            if (!fi.isDir()) continue;
            const QString canon = fi.canonicalFilePath();
            if (canon.isEmpty() || seen.contains(canon)) continue; // 去重(如下载=桌面等映射场景)
            seen.insert(canon);
            RootItem it;
            it.display = ShellDisplayName(p);
            if (it.display.isEmpty()) it.display = fi.fileName();
            it.full_path = p;
            it.icon = icon_provider.icon(fi);
            items.push_back(it);
        }

        // 排序:盘符组恒按盘符字母序,常用文件夹组按显示名响应表头排序
        const bool asc = (sort_order_ == Qt::AscendingOrder);
        std::sort(items.begin(), items.end(), [asc](const RootItem& a, const RootItem& b) {
            if (a.is_drive != b.is_drive) return a.is_drive;
            if (a.is_drive && b.is_drive) {
                return QString::compare(a.full_path, b.full_path, Qt::CaseInsensitive) < 0;
            }
            const int cmp = QString::compare(a.display, b.display, Qt::CaseInsensitive);
            return asc ? (cmp < 0) : (cmp > 0);
        });

        table_->setRowCount((int)items.size());
        int row = 0;
        for (const auto& it : items) {
            auto* item = new QTableWidgetItem(it.icon, it.display);
            item->setData(Qt::UserRole, it.full_path);
            item->setData(Qt::UserRole + 1, true); // 根视图所有项都可导航
            table_->setItem(row, 0, item);
            table_->setItem(row, 1, new QTableWidgetItem(""));
            table_->setItem(row, 2, new QTableWidgetItem(""));
            table_->setItem(row, 3, new QTableWidgetItem(EntryTypeString(true, it.is_drive, it.display)));
            ++row;
        }
    }

    void FtFilePanel::ShowDir(const QString& path, const QVector<FtEntryInfo>& entries) {
        if (is_local_) return;
        current_dir_ = path;
        last_entries_ = entries;
        RebuildBreadcrumb();
        FillRemote();
    }

    void FtFilePanel::FillRemote() {
        auto entries = last_entries_;
        // 手动排序:盘符/目录恒排前,再按选中列
        const int col = sort_col_;
        const bool asc = (sort_order_ == Qt::AscendingOrder);
        std::sort(entries.begin(), entries.end(), [col, asc](const FtEntryInfo& a, const FtEntryInfo& b) {
            // 盘符恒最前且按盘符字母序;目录/文件再按选中列
            if (a.is_drive_ != b.is_drive_) return a.is_drive_;
            if (a.is_drive_ && b.is_drive_) {
                return QString::compare(a.abs_path_, b.abs_path_, Qt::CaseInsensitive) < 0;
            }
            const bool a_dir = a.is_dir_ || a.is_drive_;
            const bool b_dir = b.is_dir_ || b.is_drive_;
            if (a_dir != b_dir) return a_dir;
            int cmp = 0;
            if (col == 1) {
                cmp = (a.size_ < b.size_) ? -1 : (a.size_ > b.size_ ? 1 : 0);
            } else if (col == 2) {
                cmp = (a.modified_time_ < b.modified_time_) ? -1 : (a.modified_time_ > b.modified_time_ ? 1 : 0);
            } else if (col == 3) {
                cmp = QString::compare(EntryTypeString(false, false, a.name_),
                                       EntryTypeString(false, false, b.name_), Qt::CaseInsensitive);
            } else {
                cmp = QString::compare(a.name_, b.name_, Qt::CaseInsensitive);
            }
            return asc ? (cmp < 0) : (cmp > 0);
        });

        table_->setRowCount(0);
        table_->setRowCount((int)entries.size());
        int row = 0;
        // 盘符/目录用通用图标;文件按扩展名取本机关联图标(远端系统图标拿不到)
        QFileIconProvider icon_provider;
        for (const auto& e : entries) {
            const QIcon icon = e.is_drive_ ? icon_provider.icon(QFileIconProvider::Drive)
                : e.is_dir_ ? icon_provider.icon(QFileIconProvider::Folder)
                            : icon_provider.icon(QFileInfo(e.name_));
            auto* item = new QTableWidgetItem(icon, e.name_);
            // 根视图常用文件夹带 abs_path(显示名 ≠ 路径),导航优先用 abs_path
            item->setData(Qt::UserRole, e.abs_path_.isEmpty() ? EntryFullPath(e.name_) : e.abs_path_);
            item->setData(Qt::UserRole + 1, e.is_dir_ || e.is_drive_);
            table_->setItem(row, 0, item);
            auto* size_item = new QTableWidgetItem(
                (e.is_dir_ || e.is_drive_) ? "" : FormatSize(e.size_));
            size_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            table_->setItem(row, 1, size_item);
            table_->setItem(row, 2, new QTableWidgetItem(FormatTime(e.modified_time_)));
            table_->setItem(row, 3, new QTableWidgetItem(
                EntryTypeString(e.is_dir_, e.is_drive_, e.name_)));
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
            if (!d.cdUp()) return {}; // 盘符根 -> 根视图
            const QString p = d.absolutePath();
            // "C:/" 再往上就是根视图
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
