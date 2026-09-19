#pragma once

#include "network_settings_model.h"

#include "px_ui/localization.h"

#include <cstdint>

namespace px::panel::ui {

enum class NetworkPageAction : std::uint8_t {
    None,
    AuthorizationChanged,
    VerifyRequested,
    SaveRequested,
};

class NetworkSettingsPage final {
  public:
    NetworkPageAction Draw(const px::ui::Localizer& localizer);
    const NetworkSettingsDraft& Draft() const noexcept;
    void SetDraft(NetworkSettingsDraft draft);
    void SetStatus(px::ui::TextId status) noexcept;

  private:
    NetworkSettingsDraft draft_{};
    px::ui::TextId status_{px::ui::TextId::PreviewInitialStatus};
};

} // namespace px::panel::ui
