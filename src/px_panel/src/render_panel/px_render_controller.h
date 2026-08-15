//
// Created by RGAA on 2024-03-30.
//

#ifndef TC_SERVER_STEAM_TC_APP_MANAGER_H
#define TC_SERVER_STEAM_TC_APP_MANAGER_H

#include <memory>
#include <map>
#include <mutex>

#include <QProcess>

#include "px_common_new/concurrent_hashmap.h"
#include "px_common_new/response.h"

namespace px
{

    class GrContext;
    class GrServiceClient;
    class GrApplication;

    class GrRenderController {
    public:

        explicit GrRenderController(const std::shared_ptr<GrApplication>& app);
        ~GrRenderController();

        bool StartServer();
        bool StopServer();
        bool ReStart();
        void Exit();

    private:
        QString GetWorkDir();
        QString GetAppPath();
        [[nodiscard]] std::vector<std::string> GetArgs();

    private:
        std::shared_ptr<GrApplication> app_ = nullptr;
        std::shared_ptr<GrContext> context_ = nullptr;
    };

}

#endif //TC_SERVER_STEAM_TC_APP_MANAGER_H
