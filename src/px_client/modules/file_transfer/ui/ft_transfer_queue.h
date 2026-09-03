//
// ft 传输队列(三栏底栏)
// 结构对齐参考 UI:默认折叠为一条摘要栏(最近任务:图标/名称/大小/状态),
// 右侧箭头展开完整队列(文件名、方向、进度条、速度、状态、取消)。
//

#ifndef PX_CLIENT_FT_TRANSFER_QUEUE_H
#define PX_CLIENT_FT_TRANSFER_QUEUE_H

#include <QWidget>
#include <QHash>
#include <QPointer>
#include <memory>

#include "../ft_core.h"

class QLabel;
class QPushButton;
class QTableWidget;

namespace px
{

    class FtTransferQueue : public QWidget {
        Q_OBJECT
    public:
        explicit FtTransferQueue(
            std::shared_ptr<FtCore> core,
            QWidget* parent = nullptr);  // NOLINT(gammaray-raw-pointer-boundary): Qt parent ownership API

        void AddJob(int id, const QString& name, bool is_download);
        void UpdateJob(const FtJobStatusInfo& st);
        void FinishJob(int id, const QString& error_or_empty);

    signals:
        // 展开/折叠切换(展开时占满整个窗口,FtWindow 隐藏两侧文件栏)
        void SigExpandedToggled(bool expanded);

    private:
        int RowOf(int id) const;
        void UpdateStrip(); // 摘要条跟随 latest_id_
        void SetExpanded(bool expanded);
        void ToggleExpanded();
        static QString FormatSpeed(double bytes_per_sec);

    private:
        std::shared_ptr<FtCore> core_;
        QPointer<QTableWidget> table_;
        QHash<int, int> rows_; // job id -> row

        // 折叠摘要条
        QPointer<QWidget> strip_;
        QPointer<QLabel> strip_icon_;
        QPointer<QLabel> strip_text_;
        QPointer<QLabel> strip_state_;
        QPointer<QPushButton> expand_btn_;
        bool expanded_ = false; // 默认折叠(参考 UI)

        // 作业信息(摘要条显示用)
        int latest_id_ = -1;
        QHash<int, QString> names_;
        QHash<int, uint64_t> totals_;
        QHash<int, bool> downloads_;
        QHash<int, QString> states_; // 最近一次状态文本
    };

}

#endif //PX_CLIENT_FT_TRANSFER_QUEUE_H
