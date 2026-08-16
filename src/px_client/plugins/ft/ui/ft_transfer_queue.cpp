//
// ft 传输队列(三栏底栏)
//

#include "ft_transfer_queue.h"

#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "translator/px_translator.h"

namespace px
{

    FtTransferQueue::FtTransferQueue(FtCore* core, QWidget* parent)
        : QWidget(parent), core_(core) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(4, 4, 4, 4);
        root->setSpacing(4);

        auto* title = new QLabel(tcTr("id_file_trans_records"), this);
        title->setStyleSheet("font-weight: bold; font-size: 14px;");
        root->addWidget(title);

        table_ = new QTableWidget(this);
        table_->setColumnCount(6);
        table_->setHorizontalHeaderLabels({tcTr("id_file_trans_file_name"),
                                           tcTr("id_file_trans_direction"),
                                           tcTr("id_file_trans_progress"),
                                           tcTr("id_file_trans_speed"),
                                           tcTr("id_file_trans_state"),
                                           ""});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(30);
        table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setShowGrid(false);
        table_->setAlternatingRowColors(true);
        root->addWidget(table_, 1);
    }

    int FtTransferQueue::RowOf(int id) const {
        return rows_.value(id, -1);
    }

    QString FtTransferQueue::FormatSpeed(double bps) {
        if (bps < 1024) return QString::number((int)bps) + " B/s";
        if (bps < 1024 * 1024) return QString::number(bps / 1024.0, 'f', 1) + " KB/s";
        return QString::number(bps / 1024.0 / 1024, 'f', 1) + " MB/s";
    }

    void FtTransferQueue::AddJob(int id, const QString& name, bool is_download) {
        if (rows_.contains(id)) return;
        const int row = table_->rowCount();
        table_->insertRow(row);
        rows_.insert(id, row);

        table_->setItem(row, 0, new QTableWidgetItem(name));
        table_->setItem(row, 1, new QTableWidgetItem(
            tcTr(is_download ? "id_file_trans_down" : "id_file_trans_upload")));

        auto* bar = new QProgressBar(table_);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(true);
        table_->setCellWidget(row, 2, bar);

        table_->setItem(row, 3, new QTableWidgetItem(""));
        table_->setItem(row, 4, new QTableWidgetItem(tcTr("id_file_trans_waiting")));

        auto* cancel_btn = new QPushButton(tcTr("id_file_trans_cancel"), table_);
        cancel_btn->setFixedHeight(24);
        connect(cancel_btn, &QPushButton::clicked, this, [this, id]() {
            core_->CancelJob(id);
        });
        table_->setCellWidget(row, 5, cancel_btn);
    }

    void FtTransferQueue::UpdateJob(const FtJobStatusInfo& st) {
        const int row = RowOf(st.id_);
        if (row < 0) return;
        auto* bar = qobject_cast<QProgressBar*>(table_->cellWidget(row, 2));
        if (bar && st.total_size_ > 0) {
            bar->setValue((int)(st.finished_size_ * 1000 / st.total_size_));
        }
        if (auto* item = table_->item(row, 3)) {
            item->setText(FormatSpeed(st.speed_));
        }
        if (auto* item = table_->item(row, 4)) {
            if (st.file_count_ > 1) {
                item->setText(QString("%1/%2").arg(st.file_num_ + 1).arg(st.file_count_));
            } else {
                item->setText(tcTr("id_file_trans_sending"));
            }
        }
    }

    void FtTransferQueue::FinishJob(int id, const QString& error_or_empty) {
        const int row = RowOf(id);
        if (row < 0) return;
        auto* bar = qobject_cast<QProgressBar*>(table_->cellWidget(row, 2));
        auto* state_item = table_->item(row, 4);
        if (error_or_empty.isEmpty()) {
            if (bar) bar->setValue(1000);
            if (state_item) state_item->setText(tcTr("id_file_trans_success"));
        } else if (error_or_empty == "cancel") {
            if (state_item) state_item->setText(tcTr("id_file_trans_state_cancel"));
        } else if (error_or_empty == "skipped") {
            if (state_item) state_item->setText(tcTr("id_file_trans_failed_cause_skip"));
        } else {
            if (state_item) state_item->setText(tcTr("id_file_trans_state_failed") + error_or_empty);
        }
        // 完成后禁掉取消按钮
        if (auto* btn = qobject_cast<QPushButton*>(table_->cellWidget(row, 5))) {
            btn->setEnabled(false);
        }
    }

}
