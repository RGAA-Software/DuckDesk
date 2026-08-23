//
// Created by RGAA on 17/05/2025.
//

#ifndef PX_PANEL_CONSOLE_CLIENT_H
#define PX_PANEL_CONSOLE_CLIENT_H

#include <memory>
#include <string>

namespace px
{
    class PxContext;

    // Between Panel <-> Console
    // Created by Make(), which picks a ws/wss client by PxSettings::IsConsoleSslEnabled().
    class PxConsoleClient {
    public:
        virtual ~PxConsoleClient() = default;

        static std::shared_ptr<PxConsoleClient> Make(const std::shared_ptr<PxContext>& ctx,
                                                 const std::string& host,
                                                 int port,
                                                 const std::string& device_id);

        virtual void Start() = 0;
        virtual void Stop() = 0;
        virtual bool IsStarted() = 0;
        virtual bool IsActive() = 0;
        virtual void PostBinMessage(const std::string& m) = 0;
        virtual bool IsAlive() const = 0;
    };

}

#endif //PX_PANEL_CONSOLE_CLIENT_H
