//
// ft 文件列表面板(本地/远程共用) — rustdesk 协议迁移阶段 3
// 三栏布局中的左/右栏:导航工具条 + 面包屑 + 文件表(名称/大小/修改时间/类型)。
// 本地栏直接读 QDir;远程栏经 FtCore::ReadDir 驱动,ShowDir 回显。
// 本地栏根视图("此电脑")= 盘符 + 常用文件夹(桌面/下载/文档/图片/音乐/视频/用户目录)。
//

#ifndef PX_CLIENT_FT_FILE_PANEL_H
#define PX_CLIENT_FT_FILE_PANEL_H

#include <QWidget>
#include <QTableWidget>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPointer>
#include <memory>

#include "../ft_core.h"

class QHBoxLayout;
class QLineEdit;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QLabel;
class QMimeData;

namespace px
{

    class FtCore;

    // 面板内文件表:重写 mimeData 携带面板来源(本地/远程)与选中路径;
    // 整行 hover(与选中同色),QTableWidget 原生 hover 只有单元格级
    class FtFileTable : public QTableWidget {
        Q_OBJECT
    public:
        explicit FtFileTable(bool is_local, QWidget* parent = nullptr);
        QStringList SelectedPaths() const; // 选中项的完整路径(由面板填充 UserRole)
        void ClearHover(); // 重填数据前复位 hover 行
    protected:
        QMimeData* mimeData(const QList<QTableWidgetItem*>& items) const override;
        void mouseMoveEvent(QMouseEvent* event) override;
        void leaveEvent(QEvent* event) override;
        void paintEvent(QPaintEvent* event) override; // 叠加整行 hover 高亮
    private:
        void SetHoverRow(int row);
    private:
        bool is_local_ = false;
        int hover_row_ = -1;
    };

    class FtFilePanel : public QWidget {
        Q_OBJECT
    public:
        FtFilePanel(
            std::shared_ptr<FtCore> core, bool is_local,
            QWidget* parent = nullptr);  // NOLINT(gammaray-raw-pointer-boundary): Qt parent ownership API

        QString CurrentDir() const { return current_dir_; }
        bool IsLocal() const { return is_local_; }

        // 远程栏:read_dir 回包回显(UI 线程)
        void ShowDir(const QString& path, const FtEntryList& entries);
        // 两侧一致:回到根视图("此电脑",本地 "" 对应远程 "/")
        void NavigateHome();
        void Refresh();
        // 远程栏标题追加对端标识(设备 ID 或名称)
        void SetDeviceName(const QString& name);

    signals:
        // 传输请求(由 FtWindow 接线到 FtCore)
        void SigUploadRequested(const QStringList& local_paths);
        void SigDownloadRequested(const QStringList& remote_paths);

    private:
        void NavigateTo(const QString& path);
        void RefreshLocal();
        void FillRemote(); // 按当前排序填充远程栏(数据源 last_entries_)
        void FillLocalDrivesAndPinned(); // 本地根视图:盘符 + 常用文件夹
        void RebuildBreadcrumb();
        void OnSortRequested(int col);
        QString EntryFullPath(const QString& name) const;
        QString ParentDirOf(const QString& path) const;
        QStringList SelectedPaths() const;
        void OnDoubleClick(int row);
        void NavigateParent();
        void OnCellDoubleClicked(int row, int column);
        void OnContextMenu(const QPoint& pos);
        void UpdateTransferBtn(); // 按选中状态切换高亮(蓝)/普通(灰)
        void DoNewFolder();
        void DoRename();
        void DoDelete();
        void DoTransferSelected();

        // drag & drop
        void dragEnterEvent(QDragEnterEvent* event) override;
        void dropEvent(QDropEvent* event) override;
        void resizeEvent(QResizeEvent* event) override; // 宽度变化时重算面包屑折叠

    private:
        std::shared_ptr<FtCore> core_;
        bool is_local_ = false;
        QString current_dir_; // 本地:"" 表示根视图(此电脑);远程:"/" 表示盘符列表

        QPointer<QHBoxLayout> crumb_bar_; // 面包屑(重建式)
        QPointer<FtFileTable> table_;
        QPointer<QPushButton> transfer_btn_;
        QPointer<QLabel> title_label_;

        // 排序状态(两栏各自独立):默认按名称升序,目录/盘符恒排前
        int sort_col_ = 0;
        Qt::SortOrder sort_order_ = Qt::AscendingOrder;
        FtEntryList last_entries_; // 远程栏最近回包,排序刷新用
    };

}

#endif //PX_CLIENT_FT_FILE_PANEL_H
