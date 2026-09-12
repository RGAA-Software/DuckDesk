#pragma once

#include "px_ui/localization.h"

namespace px::panel::ui {

class VoiceCallConsentOverlay {
  public:
    virtual ~VoiceCallConsentOverlay() = default;
    virtual void Draw(const px::ui::Localizer& localizer) = 0;
};

} // namespace px::panel::ui
