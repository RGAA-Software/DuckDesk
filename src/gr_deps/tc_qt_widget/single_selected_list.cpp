//
// Created by RGAA on 26/08/2024.
//

#include <QLabel>
#include "single_selected_list.h"
#include "clickable_widget.h"
#include "no_margin_layout.h"
#include "tc_common_new/log.h"

#include <QTimer>
#include <QStyledItemDelegate>

namespace tc
{

    // To fix the position of item
    class ItemDelegate : public QStyledItemDelegate {
    public:
        explicit ItemDelegate(QObject* parent) {}
        ~ItemDelegate() override = default;

        void updateEditorGeometry(QWidget* editor, const QStyleOptionViewItem& option, const QModelIndex& index) const override {
            editor->setGeometry(option.rect);
        }
    };

    SingleSelectedList::SingleSelectedList(QWidget* parent) : QWidget(parent) {
        list_ = new QListWidget(this);
        list_->setItemDelegate(new ItemDelegate(this));
        list_->setSpacing(0);
        list_->setContentsMargins(0,0,0,0);
        list_->setResizeMode(QListView::ResizeMode::Fixed);
        list_->setMovement(QListView::Movement::Static);
        list_->setFlow(QListView::Flow::TopToBottom);
        list_->setHorizontalScrollBarPolicy(Qt::ScrollBarPolicy::ScrollBarAlwaysOff);
        list_->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
        list_->setStyleSheet(R"(QListWidget {
                                    background: #ffffff;
                                    border: none;
                                    outline: 0;
                                    padding: 0;
                                    margin: 0;
                                    gridline-color: transparent;    /* 去除网格线 */
                                }
                             QListWidget::item:selected { background-color: transparent; })");

        auto layout = new NoMarginVLayout();
        layout->addWidget(list_);
        setLayout(layout);
    }

    QListWidgetItem* SingleSelectedList::AddItem(const SingleItemPtr& data, int index) {
        auto item = new QListWidgetItem(list_);
        item->setSizeHint(QSize(200, 38));
        auto widget = new ClickableWidget(list_);
        widget->setFixedSize(item->sizeHint());
        auto root_layout = new NoMarginHLayout();

        auto name = new QLabel(widget);
        name->setObjectName("name");
        name->setText(data->name_);
        name->setStyleSheet("padding-left: 10px;");
        root_layout->addWidget(name);

        auto origin = new QLabel(widget);
        origin->setObjectName("origin");
        origin->setText("✪");
        origin->hide();
        root_layout->addSpacing(10);
        root_layout->addWidget(origin);

        auto icon_size = QSize(40, 40);
        auto icon = new QLabel(widget);
        icon->setObjectName("icon");
        icon->hide();
        icon->setFixedSize(icon_size);
        icon->setStyleSheet(R"( background-image: url(:resources/image/ic_check.svg);
                                    background-repeat:no-repeat;
                                    background-position: center center;)");
        select_widgets_.insert({index, widget});
        root_layout->addStretch();
        root_layout->addWidget(icon);
        root_layout->addSpacing(10);
        widget->setLayout(root_layout);
        list_->setItemWidget(item, widget);

        widget->SetOnClickListener([=, this](auto w) {
            Select(index);
            if (item_click_listener_) {
                item_click_listener_(index, w);
            }
        });

        return item;
    }

    void SingleSelectedList::UpdateItems(const std::vector<SingleItemPtr> &items) {
        list_->clear();
        select_widgets_.clear();
        list_->setFixedWidth(this->width());
        items_ = items;
        int index = 0;
        for (const auto& item : items_) {
            AddItem(item, index++);
        }
        list_->update();
        list_->updateGeometry();
    }

    void SingleSelectedList::Select(int idx) const {
        if (idx < 0 || idx >= list_->count()) {
            return;
        }
        for (const auto[i, widget] : select_widgets_) {
            auto ic = widget->findChild<QLabel*>("icon");
            if (ic == nullptr) {
                continue;
            }
            if (i == idx) {
                ic->show();
            } else {
                ic->hide();
            }
        }
    }

    std::vector<SingleItemPtr> SingleSelectedList::GetItems() const {
        return items_;
    }

    void SingleSelectedList::SetOnItemClickListener(OnItemClickListener&& listener) {
        item_click_listener_ = listener;
    }

    void SingleSelectedList::SelectByName(const std::string& name) {
        for (const auto[i, widget] : select_widgets_) {
            auto lbl_name = widget->findChild<QLabel*>("name");
            if (lbl_name == nullptr) {
                continue;
            }
            if (lbl_name->text().toStdString() == name) {
                Select(i);
                break;
            }
        }
    }
}