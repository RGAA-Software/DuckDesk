//
// Created by RGAA on 30/04/2025.
//

#ifndef PX_PANEL_ST_SECURITY_FILE_TRANSFER_ITEM_H
#define PX_PANEL_ST_SECURITY_FILE_TRANSFER_ITEM_H

#include <QWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QBrush>
#include <QPen>
#include <QLabel>

namespace px
{

    class PxContext;
    class PxApplication;
    class FileTransferRecord;

    class StSecurityFileTransferItemWidget : public QWidget {
    public:
        StSecurityFileTransferItemWidget(const std::shared_ptr<PxApplication>& app,
                           const std::shared_ptr<FileTransferRecord>& item_info,
                           QWidget* parent);
        void paintEvent(QPaintEvent *event) override;
        void UpdateStatus();
        void enterEvent(QEnterEvent *event) override;
        void leaveEvent(QEvent *event) override;

    private:
        void UpdatePluginStatus(bool enabled);
        void SwitchPluginStatusInner(bool enabled);

    private:
        std::shared_ptr<FileTransferRecord> item_info_;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        QLabel* lbl_enabled_ = nullptr;
        bool enter_ = false;
    };

}

#endif //PX_ST_PLUGIN_ITEM_WIDGET_H
