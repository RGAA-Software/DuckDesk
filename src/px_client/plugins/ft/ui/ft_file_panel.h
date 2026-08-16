//
// ft 文件列表面板(本地/远程共用) — rustdesk 协议迁移阶段 3
// 三栏布局中的左/右栏:导航工具条 + 文件表(名称/大小/修改时间)。
// 本地栏直接读 QDir;远程栏经 FtCore::ReadDir 驱动,ShowDir 回显。
//

#ifndef PX_CLIENT_FT_FILE_PANEL_H
#define PX_CLIENT_FT_FILE_PANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QString>
#include <QStringList>
#include <QVector>

#include "../ft_core.h"

class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QMimeData;

namespace px
{

    class FtCore;

    // 面板内文件表:重写 mimeData 携带面板来源(本地/远程)与选中路径
    class FtFileTable : public QTableWidget {
        Q_OBJECT
    public:
        explicit FtFileTable(bool is_local, QWidget* parent = nullptr);
        QStringList SelectedPaths() const; // 选中项的完整路径(由面板填充 UserRole)
    protected:
        QMimeData* mimeData(const QList<QTableWidgetItem*>& items) const override;
    private:
        bool is_local_ = false;
    };

    class FtFilePanel : public QWidget {
        Q_OBJECT
    public:
        FtFilePanel(FtCore* core, bool is_local, QWidget* parent = nullptr);

        QString CurrentDir() const { return current_dir_; }
        bool IsLocal() const { return is_local_; }

        // 远程栏:read_dir 回包回显(UI 线程)
        void ShowDir(const QString& path, const QVector<FtEntryInfo>& entries);
        // 本地栏:初始导航到用户主目录;远程栏:导航到 "/"(盘符列表)
        void NavigateHome();
        void Refresh();

    signals:
        // 传输请求(由 FtWindow 接线到 FtCore)
        void SigUploadRequested(const QStringList& local_paths);
        void SigDownloadRequested(const QStringList& remote_paths);

    private:
        void NavigateTo(const QString& path);
        void RefreshLocal();
        QString EntryFullPath(const QString& name) const;
        QString ParentDirOf(const QString& path) const;
        QStringList SelectedPaths() const;
        void OnDoubleClick(int row);
        void OnContextMenu(const QPoint& pos);
        void DoNewFolder();
        void DoRename();
        void DoDelete();
        void DoTransferSelected();

        // drag & drop
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dropEvent(QDropEvent* event) override;

    private:
        FtCore* core_ = nullptr;
        bool is_local_ = false;
        QString current_dir_; // 本地:"" 表示盘符列表;远程:"/" 表示盘符列表

        QLineEdit* path_edit_ = nullptr;
        FtFileTable* table_ = nullptr;
        QPushButton* transfer_btn_ = nullptr;
    };

}

#endif //PX_CLIENT_FT_FILE_PANEL_H
