#pragma once

#include <cstdint>
#include <string>

#include "px_ui/localization.h"

namespace px::panel::ui {

enum class ApplicationLaunchStage { StartApplication, WaitForApplication, AuthorizeConnection, PrepareClient };
enum class ApplicationLaunchStatus { Preparing, Ready, Launching, Succeeded, Failed };

struct ApplicationLaunchProgress final {
    std::uint64_t generation{};
    std::string applicationName{};
    ApplicationLaunchStage stage{ApplicationLaunchStage::StartApplication};
    ApplicationLaunchStatus status{ApplicationLaunchStatus::Preparing};
    px::ui::TextId error{px::ui::TextId::OperationFailed};
    std::string diagnostic{};
    bool awaitingConnectionRetirement{};
    std::uint64_t retirementWaitSeconds{};
};

}  // namespace px::panel::ui
