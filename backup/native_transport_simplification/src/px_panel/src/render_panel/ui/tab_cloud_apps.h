#ifndef PIXELS_PANEL_TAB_CLOUD_APPS_H
#define PIXELS_PANEL_TAB_CLOUD_APPS_H

#include "tab_base.h"

#include <QLabel>
#include <QPointer>

namespace px
{
    class AppStreamList;
    class TcLabel;

    class TabCloudApps : public TabBase {
    public:
        explicit TabCloudApps(
            const std::shared_ptr<PxApplication>& app,
            QWidget* parent = nullptr); // NOLINT(gammaray-raw-pointer-boundary) Qt parent ABI.
        ~TabCloudApps() override;

        void OnTabShow() override;
        void resizeEvent(QResizeEvent* event) override; // NOLINT(gammaray-raw-pointer-boundary) Qt virtual ABI.

    private:
        void RefreshApplications();
        void SetEmpty(bool empty);

        QPointer<AppStreamList> app_list_;
        QPointer<QLabel> empty_tip_;
    };
}

#endif // PIXELS_PANEL_TAB_CLOUD_APPS_H
