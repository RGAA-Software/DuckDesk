//
// Created by RGAA on 30/04/2025.
//

#ifndef PX_PANEL_TAB_PROFILE_DEVICE_ITEM_H
#define PX_PANEL_TAB_PROFILE_DEVICE_ITEM_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QBrush>
#include <QPen>
#include <QLabel>
#include "px_qt_widget/click_listener.h"

namespace px
{

    class PxContext;
    class PxApplication;
    class AccountDevice;

    class TabProfileDeviceItemWidget : public QWidget {
    public:
        TabProfileDeviceItemWidget(const std::shared_ptr<PxApplication>& app,
                           const std::shared_ptr<AccountDevice>& item_info,
                           QWidget* parent);
        void paintEvent(QPaintEvent *event) override;
        void UpdateStatus();
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;
        void mousePressEvent(QMouseEvent *event) override;
        void mouseReleaseEvent(QMouseEvent *event) override;
        void SetOnItemClickListener(OnItemValueClickListener<AccountDevice>&& listener);

    private:
        std::shared_ptr<AccountDevice> item_info_;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        QLabel* lbl_enabled_ = nullptr;
        bool enter_ = false;
        bool pressed_ = false;
        OnItemValueClickListener<AccountDevice> click_listener_;
    };

}

#endif //PX_ST_PLUGIN_ITEM_WIDGET_H
