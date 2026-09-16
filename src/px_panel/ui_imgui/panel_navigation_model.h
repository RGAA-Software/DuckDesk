#pragma once

#include "panel_page.h"

#include "px_ui/localization.h"
#include "px_ui/vector_icon.h"
#include "version_config.h"

#include <array>

namespace px::panel::ui {

struct PanelNavigationItemSpec final {
    PanelPage page{};
    px::ui::TextId text{};
    px::ui::VectorIcon icon{};
};

inline constexpr auto kProductNavigationItems = std::array {
    PanelNavigationItemSpec{PanelPage::RemoteControl, px::ui::TextId::RemoteControl, px::ui::VectorIcon::Monitor},
        PanelNavigationItemSpec{PanelPage::DeviceList, px::ui::TextId::DeviceList, px::ui::VectorIcon::List},
#if PX_CAPABILITY_CLOUD_APP_CATALOG
        PanelNavigationItemSpec{PanelPage::CloudApplications, px::ui::TextId::CloudApplications, px::ui::VectorIcon::Cloud},
#endif
#if PX_CAPABILITY_DESKTOP_HOST
        PanelNavigationItemSpec{PanelPage::ServerStatus, px::ui::TextId::ServerStatus, px::ui::VectorIcon::Activity},
#endif
        PanelNavigationItemSpec{PanelPage::Settings, px::ui::TextId::Settings, px::ui::VectorIcon::Settings},
};

} // namespace px::panel::ui
