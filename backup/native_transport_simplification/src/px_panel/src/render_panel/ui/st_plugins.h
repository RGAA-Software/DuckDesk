//
// Created by RGAA on 29/04/2025.
//

#ifndef PX_ST_PLUGINS_H
#define PX_ST_PLUGINS_H

#include "tab_base.h"
#include <QListWidget>
#include <QPointer>

#include <memory>
#include <string>
#include <vector>

namespace pxrp
{
    class RpPluginInfo;
}

namespace px
{

    class PluginItemInfo {
    public:
        std::string id_;
        std::shared_ptr<pxrp::RpPluginInfo> info_ = nullptr;
    };

    class StPlugins : public TabBase {
    public:
        explicit StPlugins(const std::shared_ptr<PxApplication>& app, QWidget* parent = nullptr);
        ~StPlugins() override = default;

        void OnTabShow() override;
        void OnTabHide() override;

    private:
        void ApplyItems(std::vector<std::shared_ptr<PluginItemInfo>> items);
        void UpdateItemStatuses();
        void AddItem(const std::shared_ptr<PluginItemInfo>& item, int index);

    private:
        QPointer<QListWidget> stream_list_;
        std::vector<std::shared_ptr<PluginItemInfo>> items_info_;
    };

}



#endif //PX_ST_PLUGINS_H
