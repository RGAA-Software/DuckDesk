//
// ft 传输队列(三栏底栏)
//

#include "ft_transfer_queue.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QIcon>
#include <QLabel>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include "px_ft_engine/ft_terminal.h"
#include "translator/px_translator.h"

namespace px
{
    namespace
    {
        QString TerminalReasonText(const ft::FtTerminalInfo& terminal) {
            if (terminal.reason == "integrity_mismatch" ||
                terminal.reason == "integrity_hash_missing" ||
                terminal.reason == "block_sequence_mismatch") {
                return tcTr("id_file_trans_failed_cause_verify");
            }
            if (terminal.reason == "permission_denied") {
                return tcTr("id_file_trans_failed_cause_permission");
            }
            if (terminal.reason == "file_count_limit") {
                return tcTr("id_file_trans_failed_cause_file_limit");
            }
            if (terminal.reason == "source_not_found") {
                return tcTr("id_file_trans_failed_cause_file_no_exists");
            }
            if (terminal.reason == "destination_busy") {
                return tcTr("id_file_trans_failed_cause_destination_busy");
            }
            if (terminal.reason == "io_error") {
                return tcTr("id_file_trans_failed_cause_file_write");
            }
            if (terminal.reason == "transport_timeout") {
                return tcTr("id_file_trans_failed_cause_time_out");
            }
            if (terminal.reason == "session_interrupted" ||
                terminal.reason == "transport_disconnected" ||
                terminal.reason == "route_unavailable" ||
                terminal.reason == "transport_error") {
                return tcTr("id_file_trans_net_error");
            }
            return tcTr("id_file_trans_failed_cause_unknow");
        }
    }

    // 表格样式与文件表一致(队列表不可选)
    static const char* kQueueTableStyle = R"(
        QTableWidget { background: transparent; border: none; alternate-background-color: #f7f9fc; }
        QHeaderView::section { background: transparent; color: #666666; border: none;
                               border-bottom: 1px solid #e0e4ea; padding: 4px; }
        QTableWidget::item { border: none; padding-left: 4px; }
    )";

    FtTransferQueue::FtTransferQueue(
        std::shared_ptr<FtCore> core, QWidget* parent)  // NOLINT(gammaray-raw-pointer-boundary): Qt parent ownership API
        : QWidget(parent), core_(std::move(core)) {
        setAttribute(Qt::WA_StyledBackground, true); // 圆角卡片背景(#ftCard)需要显式开启自绘
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 4, 6, 6);
        root->setSpacing(0);

        // 展开后的完整队列表(默认隐藏)
        table_ = new QTableWidget(this);
        table_->setColumnCount(6);
        table_->setHorizontalHeaderLabels({tcTr("id_file_trans_file_name"),
                                           tcTr("id_file_trans_direction"),
                                           tcTr("id_file_trans_progress"),
                                           tcTr("id_file_trans_speed"),
                                           tcTr("id_file_trans_state"),
                                           ""});
        table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Fixed);
        table_->setColumnWidth(1, 110);
        table_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
        table_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
        table_->setColumnWidth(3, 90);
        table_->horizontalHeader()->setSectionResizeMode(4, QHeaderView::Fixed);
        table_->setColumnWidth(4, 110);
        table_->horizontalHeader()->setSectionResizeMode(5, QHeaderView::Fixed);
        table_->setColumnWidth(5, 40);
        table_->verticalHeader()->hide();
        table_->verticalHeader()->setDefaultSectionSize(30);
        table_->setSelectionMode(QAbstractItemView::NoSelection);
        table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table_->setShowGrid(false);
        table_->setAlternatingRowColors(true);
        table_->setStyleSheet(kQueueTableStyle);
        root->addWidget(table_, 1);

        // 折叠摘要条:图标 | 名称+大小 | 方向+状态 | 展开/关闭箭头
        strip_ = new QWidget(this);
        strip_->setFixedHeight(40);
        strip_->setStyleSheet("QWidget { background: transparent; }"
                              "QLabel { background: transparent; }");
        auto* sl = new QHBoxLayout(strip_);
        sl->setContentsMargins(10, 0, 6, 0);
        sl->setSpacing(8);
        strip_icon_ = new QLabel(strip_);
        strip_icon_->setFixedSize(24, 24);
        strip_text_ = new QLabel(tcTr("id_file_trans_records"), strip_);
        strip_state_ = new QLabel("", strip_);
        strip_state_->setStyleSheet("color: #666;");
        expand_btn_ = new QPushButton(strip_);
        expand_btn_->setIcon(QIcon(":/ft/icons/ic_expand_less.svg"));
        expand_btn_->setIconSize(QSize(20, 20));
        expand_btn_->setFixedSize(32, 28);
        expand_btn_->setCursor(Qt::PointingHandCursor);
        expand_btn_->setStyleSheet("QPushButton { background: transparent; border: none; }"
                                   "QPushButton:hover { background: #e3e9f3; border-radius: 4px; }");
        sl->addWidget(strip_icon_);
        sl->addWidget(strip_text_);
        sl->addStretch(1);
        sl->addWidget(strip_state_);
        sl->addWidget(expand_btn_);
        root->addWidget(strip_);
        strip_icon_->hide();

        connect(expand_btn_.get(), &QPushButton::clicked, this,
                &FtTransferQueue::ToggleExpanded);
        SetExpanded(false);
    }

    void FtTransferQueue::ToggleExpanded() {
        SetExpanded(!expanded_);
    }

    void FtTransferQueue::SetExpanded(bool expanded) {
        expanded_ = expanded;
        table_->setVisible(expanded_);
        expand_btn_->setIcon(QIcon(expanded_ ? ":/ft/icons/ic_expand_more.svg"
                                             : ":/ft/icons/ic_expand_less.svg"));
        emit SigExpandedToggled(expanded_);
    }

    int FtTransferQueue::RowOf(int id) const {
        return rows_.value(id, -1);
    }

    QString FtTransferQueue::FormatSpeed(double bps) {
        if (bps < 1024) return QString::number((int)bps) + " B/s";
        if (bps < 1024 * 1024) return QString::number(bps / 1024.0, 'f', 1) + " KB/s";
        return QString::number(bps / 1024.0 / 1024, 'f', 1) + " MB/s";
    }

    void FtTransferQueue::UpdateStrip() {
        if (latest_id_ < 0 || !names_.contains(latest_id_)) return;
        const QString& name = names_[latest_id_];
        // 摘要条图标:按扩展名取本机关联图标
        QFileIconProvider provider;
        strip_icon_->setPixmap(provider.icon(QFileInfo(name)).pixmap(24, 24));
        strip_icon_->show();
        QString text = name;
        const uint64_t total = totals_.value(latest_id_, 0);
        if (total > 0) {
            text += QString("  %1").arg(
                // 复用与文件表一致的格式化
                total < 1024 ? QString::number(total) + " B"
                : total < 1024ull * 1024 ? QString::number(total / 1024.0, 'f', 1) + " KB"
                : total < 1024ull * 1024 * 1024 ? QString::number(total / 1024.0 / 1024, 'f', 1) + " MB"
                : QString::number(total / 1024.0 / 1024 / 1024, 'f', 2) + " GB");
        }
        strip_text_->setText(text);
        const QString dir = tcTr(downloads_.value(latest_id_) ? "id_file_trans_down" : "id_file_trans_upload");
        const QString state = states_.value(latest_id_, tcTr("id_file_trans_waiting"));
        strip_state_->setText(QString("%1 · %2").arg(dir, state));
    }

    void FtTransferQueue::AddJob(int id, const QString& name, bool is_download) {
        if (rows_.contains(id)) return;
        const int row = table_->rowCount();
        table_->insertRow(row);
        rows_.insert(id, row);

        names_[id] = name;
        downloads_[id] = is_download;
        states_[id] = tcTr("id_file_trans_waiting");
        latest_id_ = id;
        UpdateStrip();

        table_->setItem(row, 0, new QTableWidgetItem(name));
        table_->setItem(row, 1, new QTableWidgetItem(
            tcTr(is_download ? "id_file_trans_down" : "id_file_trans_upload")));

        // 6px 圆角细进度条,外层容器垂直居中
        auto* bar_holder = new QWidget(table_);
        auto* bar_layout = new QHBoxLayout(bar_holder);
        bar_layout->setContentsMargins(8, 0, 8, 0);
        auto* bar = new QProgressBar(bar_holder);
        bar->setRange(0, 1000);
        bar->setValue(0);
        bar->setTextVisible(false);
        bar->setFixedHeight(6);
        bar->setStyleSheet("QProgressBar { border: none; background: #e6e6e6; border-radius: 3px; }"
                           "QProgressBar::chunk { background: #2979ff; border-radius: 3px; }");
        bar_layout->addWidget(bar);
        table_->setCellWidget(row, 2, bar_holder);

        table_->setItem(row, 3, new QTableWidgetItem(""));
        table_->setItem(row, 4, new QTableWidgetItem(tcTr("id_file_trans_waiting")));

        // 圆形小取消按钮(close 图标)
        auto* cancel_btn = new QPushButton(table_);
        cancel_btn->setIcon(QIcon(":/ft/icons/ic_close.svg"));
        cancel_btn->setIconSize(QSize(12, 12));
        cancel_btn->setToolTip(tcTr("id_file_trans_cancel"));
        cancel_btn->setFixedSize(20, 20);
        cancel_btn->setStyleSheet(
            "QPushButton { border: 1px solid #ccc; border-radius: 10px; background: #f5f5f5; }"
            "QPushButton:hover { background: #ffebee; border-color: #d32f2f; }"
            "QPushButton:disabled { background: #fafafa; border-color: #e0e0e0; }");
        connect(cancel_btn, &QPushButton::clicked, this, [this, id]() {
            core_->CancelJob(id);
        });
        table_->setCellWidget(row, 5, cancel_btn);
    }

    void FtTransferQueue::UpdateJob(const FtJobStatusInfo& st) {
        const int row = RowOf(st.id_);
        if (row < 0) return;
        auto* holder = table_->cellWidget(row, 2);
        auto* bar = holder ? holder->findChild<QProgressBar*>() : nullptr;
        if (bar && st.total_size_ > 0) {
            bar->setValue((int)(st.finished_size_ * 1000 / st.total_size_));
        }
        totals_[st.id_] = st.total_size_;
        if (auto* item = table_->item(row, 3)) {
            item->setText(FormatSpeed(st.speed_));
        }
        QString state;
        if (st.file_count_ > 1) {
            state = QString("%1/%2").arg(st.file_num_ + 1).arg(st.file_count_);
        } else {
            state = tcTr("id_file_trans_sending");
        }
        if (auto* item = table_->item(row, 4)) {
            item->setText(state);
        }
        states_[st.id_] = state;
        if (st.id_ == latest_id_) UpdateStrip();
    }

    void FtTransferQueue::FinishJob(int id, const QString& error_or_empty) {
        const int row = RowOf(id);
        if (row < 0) return;
        const QPointer<QWidget> holder(table_->cellWidget(row, 2));
        const QPointer<QProgressBar> bar(
            holder ? holder->findChild<QProgressBar*>() : nullptr);
        QString state;
        if (error_or_empty.isEmpty()) {
            if (bar) bar->setValue(1000);
            state = tcTr("id_file_trans_success");
        } else {
            const auto terminal = ft::ClassifyTerminal(error_or_empty.toStdString());
            if (terminal.status == "cancelled") {
                state = tcTr("id_file_trans_state_cancel");
            } else if (terminal.status == "skipped") {
                state = tcTr("id_file_trans_failed_cause_skip");
            } else {
                state = tcTr("id_file_trans_state_failed") + TerminalReasonText(terminal);
                if (terminal.resumable) {
                    state += QString(" (%1)").arg(tcTr("id_file_trans_resume_available"));
                }
            }
        }
        if (table_->item(row, 4)) table_->item(row, 4)->setText(state);
        states_[id] = state;
        if (id == latest_id_) UpdateStrip();
        // 完成后禁掉取消按钮
        const QPointer<QPushButton> cancel_button(
            qobject_cast<QPushButton*>(table_->cellWidget(row, 5)));
        if (cancel_button) {
            cancel_button->setEnabled(false);
        }
    }

}
