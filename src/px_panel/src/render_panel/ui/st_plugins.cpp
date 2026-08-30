//
// Created by RGAA on 29/04/2025.
//

#include "st_plugins.h"
#include <QPointer>
#include "render_panel/px_application.h"
#include "render_panel/px_app_messages.h"
#include "px_common_new/message_notifier.h"
#include "px_render_panel_message.pb.h"
#include "st_plugin_item_widget.h"
#include "no_margin_layout.h"

#include <algorithm>
#include <utility>

namespace px
{

    StPlugins::StPlugins(const std::shared_ptr<PxApplication>& app, QWidget* parent) : TabBase(app, parent) {
        auto root_layout = new NoMarginVLayout();
        stream_list_ = new QListWidget(this);

        stream_list_->setMovement(QListView::Static);
        stream_list_->setViewMode(QListView::ListMode);
        stream_list_->setFlow(QListView::TopToBottom);
        stream_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        stream_list_->setResizeMode(QListWidget::Adjust);
        stream_list_->setSpacing(0);
        stream_list_->setStyleSheet(R"(
            QListWidget {
                background-color: #ffffff;
            }
            QListWidget::item {
                color: #ffffff;
                border: transparent;
                border-bottom: 0px solid #dbdbdb;
            }

            QListWidget::item:hover {
                background-color: none;
            }

            QListWidget::item:selected {
                border-left: 0px solid #777777;
                background-color: none;
            }
        )");

        root_layout->addWidget(stream_list_);

        setLayout(root_layout);

        QPointer<StPlugins> self(this);
        msg_listener_->Listen<MsgPluginsInfo>([self](const MsgPluginsInfo& m_info) {
            if (!self || !m_info.plugins_info_) {
                return;
            }
            std::vector<std::shared_ptr<PluginItemInfo>> items;
            items.reserve(m_info.plugins_info_->plugins_info_size());
            for (const auto& new_info : m_info.plugins_info_->plugins_info()) {
                auto plugin_info = std::make_shared<pxrp::RpPluginInfo>();
                plugin_info->CopyFrom(new_info);
                items.push_back(std::make_shared<PluginItemInfo>(PluginItemInfo{
                    .id_ = new_info.id(),
                    .info_ = std::move(plugin_info),
                }));
            }
            self->ApplyItems(std::move(items));
        });

        setObjectName("StPlugins");
        setStyleSheet("#StPlugins {background-color: #ffffff;}");
    }

    void StPlugins::OnTabShow() {

    }

    void StPlugins::OnTabHide() {

    }

    void StPlugins::AddItem(const std::shared_ptr<PluginItemInfo>& item_info, int index) {
        auto item = new QListWidgetItem(stream_list_);
        auto item_size = QSize(955, 60);
        item->setSizeHint(item_size);
        auto widget = new StPluginItemWidget(app_, item_info, index, stream_list_);
        widget->setFixedSize(item_size);
        stream_list_->setItemWidget(item, widget);
    }

    void StPlugins::ApplyItems(
        std::vector<std::shared_ptr<PluginItemInfo>> items) {
        if (!stream_list_) {
            return;
        }
        const bool same_layout = items.size() == items_info_.size()
            && std::equal(
                items.begin(), items.end(), items_info_.begin(),
                [](const auto& incoming, const auto& existing) {
                    return incoming && existing && incoming->id_ == existing->id_;
                });
        if (same_layout) {
            for (std::size_t index = 0; index < items.size(); ++index) {
                if (items_info_.at(index)->info_ && items.at(index)->info_) {
                    items_info_.at(index)->info_->CopyFrom(
                        *items.at(index)->info_);
                }
            }
            UpdateItemStatuses();
            return;
        }

        items_info_ = std::move(items);
        stream_list_->clear();
        int index = 0;
        for (const auto& item_info : items_info_) {
            AddItem(item_info, index++);
        }
    }

    void StPlugins::UpdateItemStatuses() {
        if (!stream_list_) {
            return;
        }
        for (int index = 0; index < stream_list_->count(); ++index) {
            const QPointer<StPluginItemWidget> item_widget(
                static_cast<StPluginItemWidget*>(
                    stream_list_->itemWidget(stream_list_->item(index))));
            if (item_widget) {
                item_widget->UpdateStatus();
            }
        }
    }

}
