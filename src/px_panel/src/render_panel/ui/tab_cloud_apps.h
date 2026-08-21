#ifndef PIXELS_PANEL_TAB_CLOUD_APPS_H
#define PIXELS_PANEL_TAB_CLOUD_APPS_H

#include "tab_base.h"

#include <QLabel>

namespace px
{
    class AppStreamList;
    class TcLabel;

    class TabCloudApps : public TabBase {
    public:
        explicit TabCloudApps(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~TabCloudApps() override;

        void OnTabShow() override;
        void resizeEvent(QResizeEvent* event) override;

    private:
        void RefreshApplications();
        void SetEmpty(bool empty);

        AppStreamList* app_list_ = nullptr;
        QLabel* empty_tip_ = nullptr;
    };
}

#endif // PIXELS_PANEL_TAB_CLOUD_APPS_H
