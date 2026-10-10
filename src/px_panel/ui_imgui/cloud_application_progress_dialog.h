#pragma once

#include "cloud_applications_port.h"

namespace px::panel::ui {

class CloudApplicationProgressDialog final {
public:
    explicit CloudApplicationProgressDialog(std::shared_ptr<CloudApplicationsPort> port);
    void Draw(const px::ui::Localizer& localizer);

private:
    std::shared_ptr<CloudApplicationsPort> port_{};
    std::uint64_t observedGeneration_{};
    std::uint64_t dispatchedGeneration_{};
    ApplicationLaunchStatus displayedStatus_{ApplicationLaunchStatus::Succeeded};
};

}  // namespace px::panel::ui
