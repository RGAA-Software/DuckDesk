//
// ft 传输队列(三栏底栏) — 每作业:文件名、方向、进度条、速度、状态、取消按钮
//

#ifndef PX_CLIENT_FT_TRANSFER_QUEUE_H
#define PX_CLIENT_FT_TRANSFER_QUEUE_H

#include <QWidget>
#include <QHash>

#include "../ft_core.h"

class QTableWidget;
class QLabel;

namespace px
{

    class FtTransferQueue : public QWidget {
        Q_OBJECT
    public:
        explicit FtTransferQueue(FtCore* core, QWidget* parent = nullptr);

        void AddJob(int id, const QString& name, bool is_download);
        void UpdateJob(const FtJobStatusInfo& st);
        void FinishJob(int id, const QString& error_or_empty);

    private:
        int RowOf(int id) const;
        static QString FormatSpeed(double bytes_per_sec);

    private:
        FtCore* core_ = nullptr;
        QTableWidget* table_ = nullptr;
        QHash<int, int> rows_; // job id -> row
    };

}

#endif //PX_CLIENT_FT_TRANSFER_QUEUE_H
