#pragma once

#include "px_ui/components/feedback.h"
#include "px_ui/px_ui_theme.h"

#include <string>

namespace px::panel::ui {

struct ComponentGalleryAction final {
    bool exitRequested{false};
    bool themeChanged{false};
    bool enhancedVisualEffectsChanged{false};
    bool enhancedVisualEffects{true};
    px::ui::Theme theme{px::ui::Theme::Dark};
};

class ComponentGallery final {
  public:
    [[nodiscard]] ComponentGalleryAction Draw();

  private:
    px::ui::Theme theme_{px::ui::Theme::Dark};
    px::ui::ToastHost toasts_{};
    std::string text_{};
    std::string invalidText_{};
    std::string multiline_{};
    int number_{10};
    int selection_{60};
    bool checked_{true};
    bool switched_{true};
    bool enhancedVisualEffects_{true};
    bool dialogRequested_{false};
};

} // namespace px::panel::ui
