//
// Created by RGAA on 2024-03-30.
//

#ifndef TC_SERVER_STEAM_TC_APP_MANAGER_H
#define TC_SERVER_STEAM_TC_APP_MANAGER_H

#include <memory>
#include <map>
#include <mutex>

#include <QProcess>

#include "px_common/concurrent_hashmap.h"
#include "px_common/response.h"

namespace px
{

    class PxContext;
    class PxServiceClient;
    class PxApplication;

    class PxRenderController {
    public:

        explicit PxRenderController(const std::shared_ptr<PxApplication>& app);
        ~PxRenderController();

        bool StartServer();
        bool StopServer();
        bool ReStart();
        void Exit();

    private:
        QString GetWorkDir();
        QString GetAppPath();
        [[nodiscard]] std::vector<std::string> GetArgs();

    private:
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
    };

}

#endif //TC_SERVER_STEAM_TC_APP_MANAGER_H
