//
// Created by RGAA on 28/05/2025.
//

#include "st_security_visitor.h"
#include "no_margin_layout.h"
#include "px_pushbutton.h"
#include "px_label.h"
#include "px_qt_widget/pagination/page_widget.h"
#include "st_security_visitor_item.h"
#include "render_panel/px_context.h"
#include "render_panel/px_app_messages.h"
#include "render_panel/database/px_database.h"
#include "render_panel/database/visit_record.h"
#include "render_panel/database/visit_record_operator.h"
#include "px_dialog.h"
#include "px_image_button.h"
#include "security_password_checker.h"
#include "px_common_new/latest_async_generation.h"
#include <QMenu>
#include <QClipboard>
#include <QApplication>

namespace px
{

    constexpr int kPageSize = 20;

    StSecurityVisitor::StSecurityVisitor(
        const std::shared_ptr<PxApplication>& app,
        QWidget* parent) // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI; TabBase retains ownership.
        : TabBase(app, parent) {
        QPointer<StSecurityVisitor> self(this);
        visit_op_ = context_->GetDatabase()->GetVisitRecordOp();
        load_generation_ = LatestAsyncGeneration::Create();
        auto root_layout = new NoMarginVLayout();

        {
            // title
            auto layout = new NoMarginHLayout();

            auto label = new TcLabel(this);
            label->setFixedWidth(235);
            label->SetTextId("id_security_visitor_history_logs");
            label->setStyleSheet("font-size: 16px; font-weight: 700; padding-left: 7px;");
            layout->addWidget(label);

            {
                auto btn_refresh = new TcImageButton(":/resources/image/ic_refresh.svg", QSize(16, 16));
                btn_refresh->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
                btn_refresh->SetRoundRadius(13);
                btn_refresh->setFixedSize(26, 26);
                layout->addWidget(btn_refresh, 0, Qt::AlignVCenter);
                btn_refresh->SetOnImageButtonClicked([self]() {
                    if (self && self->page_widget_) {
                        self->LoadPage(self->page_widget_->getCurrentPage());
                    }
                });
            }

            {
                auto btn_clear_all = new TcImageButton(":/resources/image/ic_clear.svg", QSize(16, 16));
                btn_clear_all->SetColor(0xffffff, 0xdddddd, 0xbbbbbb);
                btn_clear_all->SetRoundRadius(13);
                btn_clear_all->setFixedSize(26, 26);
                layout->addSpacing(10);
                layout->addWidget(btn_clear_all, 0, Qt::AlignVCenter);
                btn_clear_all->SetOnImageButtonClicked([self]() {
                    if (!self || !self->page_widget_) return;
                    //
                    if (!SecurityPasswordChecker::ShowNoSecurityPasswordDialog()) {
                        auto input_pwd = SecurityPasswordChecker::GetSecurityPasswordDialog();
                        if (SecurityPasswordChecker::IsInputSecurityPasswordOk(input_pwd)) {
                            //
                            TcDialog dialog(tcTr("id_warning"), tcTr("id_delete_all_records"));
                            if (dialog.exec() == kDoneOk) {
                                const auto page = self->page_widget_->getCurrentPage();
                                const auto context = self->context_;
                                const auto record_operator = self->visit_op_;
                                context->PostDBTask([
                                    context, record_operator, self, page]() {
                                    record_operator->DeleteAll();
                                    context->PostUITask([self, page]() {
                                        if (self) self->LoadPage(page);
                                    });
                                });
                            }
                        }
                        else {
                            SecurityPasswordChecker::ShowSecurityPasswordInvalidDialog();
                        }
                    }
                });
            }

            layout->addStretch();
            root_layout->addLayout(layout);
        }

        {
            list_widget_ = new QListWidget(this);

            list_widget_->setMovement(QListView::Static);
            list_widget_->setViewMode(QListView::ListMode);
            list_widget_->setFlow(QListView::TopToBottom);
            list_widget_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
            list_widget_->setResizeMode(QListWidget::Adjust);
            list_widget_->setContextMenuPolicy(Qt::CustomContextMenu);
            list_widget_->setSpacing(2);
            list_widget_->setStyleSheet(R"(
                QListWidget {
                    background-color: #ffffff;
                    border: 0px solid #ffffff;
                }
                QListWidget::item {
                    color: #ffffff;
                    border: transparent;
                    border-bottom: 0px solid #ffffff;
                }

                QListWidget::item:hover {
                    background-color: none;
                }

                QListWidget::item:selected {
                    border-left: 0px solid #777777;
                    background-color: none;
                }
            )");

            QObject::connect(list_widget_, &QListWidget::customContextMenuRequested, this, [self](const QPoint& pos) {
                if (!self || !self->list_widget_) return;
                const auto index = self->list_widget_->indexAt(pos);
                if (!index.isValid()) return;
                self->RegisterActions(index.row());
            });

            QObject::connect(list_widget_, &QListWidget::itemDoubleClicked, this,
                [self](QListWidgetItem* item) { // NOLINT(gammaray-raw-pointer-boundary) Qt signal ABI; never retained.
                if (!self || !self->list_widget_ || !item) return;
                const int index = self->list_widget_->row(item);
                self->ProcessCopy(self->records_.at(index));
            });

            root_layout->addWidget(list_widget_);

        }

        page_widget_ = new PageWidget();
        page_widget_->setMaxPage(1);

        root_layout->addSpacing(10);
        root_layout->addWidget(page_widget_);
        root_layout->addSpacing(20);

        setLayout(root_layout);

        setObjectName("StSecurityVisitor");
        setStyleSheet("#StSecurityVisitor {background-color: #ffffff;}");

        page_widget_->setSelectedCallback([self](int page) {
            if (self) {
                LOGI("Will load page: {}", page);
                self->LoadPage(page);
            }
        });

        // Load Page 1
        context_->PostUIDelayTask([self]() {
            if (self) self->LoadPage(1);
        }, 500);
    }

    StSecurityVisitor::~StSecurityVisitor() {
        load_generation_->Stop();
    }

    void StSecurityVisitor::AddItem(const std::shared_ptr<VisitRecord>& item_info) {
        auto item = new QListWidgetItem(list_widget_);
        auto item_size = QSize(995, 45);
        item->setSizeHint(item_size);
        auto widget = new StSecurityVisitorItemWidget(app_, item_info, list_widget_);
        if (item_info->IsHeaderItem()) {
            header_item_ = widget;
        }
        widget->setFixedSize(item_size);
        list_widget_->setItemWidget(item, widget);
    }

    void StSecurityVisitor::LoadPage(int page) {
        if (!list_widget_) {
            return;
        }

        const auto context = context_;
        const auto record_operator = visit_op_;
        const auto load_generation = load_generation_;
        const auto generation = load_generation->Begin();
        if (generation == 0) return;
        QPointer<StSecurityVisitor> self(this);
        context->PostDBTask([
            context, record_operator, load_generation, self, page, generation]() {
            const auto total_count = record_operator->GetTotalCounts();
            const auto page_result =
                std::make_shared<std::vector<std::shared_ptr<VisitRecord>>>();
            page_result->push_back(std::make_shared<VisitRecord>(VisitRecord {
                .id_ = 0,
                .connection_type_ = "",
                .begin_ = 1,
                .end_ = 1,
                .duration_ = 10,
                .visitor_device_ = "",
                .target_device_ = "",
            }));

            auto records = record_operator->QueryVisitRecords(page, kPageSize);
            for (const auto& r : records) {
                page_result->push_back(r);
            }

            context->PostUITask([
                load_generation, self, page_result, total_count, generation]() {
                if (!load_generation->Complete(generation) || !self
                    || !self->page_widget_ || !self->list_widget_) return;
                self->records_ = *page_result;
                self->header_item_.clear();
                self->page_widget_->setMaxPage(total_count/kPageSize+1);
                const int count = self->list_widget_->count();
                for (int i = 0; i < count; i++) {
                    auto item = self->list_widget_->takeItem(0);
                    delete item;
                }

                for (const auto& item_info : self->records_) {
                    self->AddItem(item_info);
                }
            });
        });
    }

    void StSecurityVisitor::RegisterActions(int index) {
        auto record = records_.at(index);
        std::vector<QString> actions = {
                tcTr("id_copy"),
                tcTr("id_copy_as_json"),
                "",
                tcTr("id_delete"),
        };
        QMenu menu;
        QPointer<StSecurityVisitor> self(this);
        for (int i = 0; i < actions.size(); i++) {
            QString action_name = actions.at(i);
            if (action_name.isEmpty()) {
                menu.addSeparator();
                continue;
            }

            const QPointer<QAction> action(menu.addAction(action_name));
            QObject::connect(action, &QAction::triggered, this, [self, record, i]() {
                if (!self) return;
                if (i == 0) {
                    self->ProcessCopy(record);
                }
                else if (i == 1) {
                    self->ProcessCopyAsJson(record);
                }
                else if (i == 3) {
                    if (!SecurityPasswordChecker::ShowNoSecurityPasswordDialog()) {
                        auto input_pwd = SecurityPasswordChecker::GetSecurityPasswordDialog();
                        if (SecurityPasswordChecker::IsInputSecurityPasswordOk(input_pwd)) {
                            self->ProcessDelete(record);
                        }
                        else {
                            SecurityPasswordChecker::ShowSecurityPasswordInvalidDialog();
                        }
                    }
                }
            });
        }
        menu.exec(QCursor::pos());
    }

    void StSecurityVisitor::ProcessCopy(const std::shared_ptr<VisitRecord>& record) {
        auto msg = record->AsString();
        QApplication::clipboard()->setText(QString::fromStdString(msg));
        context_->NotifyAppMessage(tcTr("id_copy_success"), tcTr("id_copy_success_clipboard"));
    }

    void StSecurityVisitor::ProcessCopyAsJson(const std::shared_ptr<VisitRecord>& record) {
        auto msg = record->AsJson();
        QApplication::clipboard()->setText(QString::fromStdString(msg));
        context_->NotifyAppMessage(tcTr("id_copy_success"), tcTr("id_copy_success_clipboard"));
    }

    void StSecurityVisitor::ProcessDelete(const std::shared_ptr<VisitRecord>& record) {
        TcDialog dialog(tcTr("id_warning"), tcTr("id_delete_this_record"));
        if (dialog.exec() == kDoneOk) {
            const auto page = page_widget_->getCurrentPage();
            const auto context = context_;
            const auto record_operator = visit_op_;
            QPointer<StSecurityVisitor> self(this);
            context->PostDBTask([context, record_operator, self, record, page]() {
                record_operator->Delete(record->id_);
                context->PostUITask([self, page]() {
                    if (self) self->LoadPage(page);
                });
            });
        }
    }

    void StSecurityVisitor::OnTranslate() {

    }

}
