//
// Created by RGAA on 12/12/2025.
//

#ifndef GAMMARAYPREMIUM_GR_CONSOLE_MANAGER_H
#define GAMMARAYPREMIUM_GR_CONSOLE_MANAGER_H

#include <memory>
#include <optional>
#include "px_console_client/console_api.h"

namespace px
{

    class PxContext;
    class PxSettings;

    class PxConsoleManager {
    public:
        explicit PxConsoleManager(const std::shared_ptr<PxContext>& context);
        std::optional<px_console::AliveConnections> QueryAliveConnections(bool show_err_dialog) const;
        std::optional<px_console::AvailableNewConnection> QueryNewConnection(bool show_err_dialog) const;

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_GR_CONSOLE_MANAGER_H