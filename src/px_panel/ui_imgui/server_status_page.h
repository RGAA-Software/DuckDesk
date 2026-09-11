#pragma once

#include "server_status_port.h"

#include "px_ui/localization.h"

#include <functional>
#include <memory>

namespace px::panel::ui {

class ServerStatusPage final {
  public:
    explicit ServerStatusPage(std::shared_ptr<ServerStatusPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void DrawStatusRow(const px::ui::Localizer& localizer, px::ui::TextId label, bool ready, bool canAct, px::ui::TextId action,
                       const std::function<void()>& onAction) const;

    std::shared_ptr<ServerStatusPort> port_{};
};

} // namespace px::panel::ui
