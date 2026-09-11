#pragma once

#include "network_settings_presenter.h"
#include "panel_navigation.h"
#include "server_status_page.h"
#include "px_ui/localization.h"
#include "px_ui/px_ui_theme.h"

#include <optional>

namespace px::panel::ui {

struct PanelPreviewAction final {
    std::optional<px::ui::Theme> selectedTheme{};
    bool exitRequested{false};
};

struct PanelPreviewServices final {
    std::shared_ptr<NetworkSettingsPort> networkSettings{};
    std::shared_ptr<ServerStatusPort> serverStatus{};
};

class PanelPreview final {
  public:
    PanelPreview();
    explicit PanelPreview(PanelPreviewServices services);

    PanelPreviewAction Draw();

  private:
    PanelPreviewAction DrawNetworkPage();

    px::ui::Localizer localizer_{};
    px::ui::Theme theme_{px::ui::Theme::Dark};
    PanelNavigation navigation_{};
    NetworkSettingsPresenter networkSettings_;
    ServerStatusPage serverStatus_;
};

} // namespace px::panel::ui
