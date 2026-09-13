#pragma once

#include "cloud_applications_port.h"

#include "px_ui/localization.h"

#include <cstddef>
#include <memory>
#include <string>

namespace px::panel::ui {

class CloudApplicationsPage final {
  public:
    explicit CloudApplicationsPage(std::shared_ptr<CloudApplicationsPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void DrawApplicationCard(const CloudApplicationCard& application, const px::ui::Localizer& localizer, std::size_t index, float width);
    void DrawContextMenu(const CloudApplicationCard& application, const px::ui::Localizer& localizer);
    void DrawPasswordDialog(const px::ui::Localizer& localizer);

    std::shared_ptr<CloudApplicationsPort> port_{};
    std::string passwordStreamId_{};
    std::string password_{};
    bool passwordDialogOpen_{};
};

} // namespace px::panel::ui
