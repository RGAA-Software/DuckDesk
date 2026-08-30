//
// Created by RGAA on 30/04/2025.
//

#ifndef PX_ST_PLUGIN_ITEM_WIDGET_H
#define PX_ST_PLUGIN_ITEM_WIDGET_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QBrush>
#include <QPen>
#include <QLabel>
#include <QPointer>

namespace px
{

    class PxApplication;
    class PluginItemInfo;

    class StPluginItemWidget : public QWidget {
    public:
        StPluginItemWidget(const std::shared_ptr<PxApplication>& app,
                           const std::shared_ptr<PluginItemInfo>& item_info,
                           int index,
                           QWidget* parent);
        void paintEvent(QPaintEvent *event) override;
        void UpdateStatus();

    private:
        void UpdatePluginStatus(bool enabled);
        void SwitchPluginStatusInner(bool enabled);
        void SendRecordCommand(bool start);

    private:
        std::shared_ptr<PluginItemInfo> item_info_;
        std::shared_ptr<PxApplication> app_ = nullptr;
        QPointer<QLabel> lbl_enabled_;
    };

}

#endif //PX_ST_PLUGIN_ITEM_WIDGET_H
