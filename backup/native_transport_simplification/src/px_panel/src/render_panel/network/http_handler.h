//
// Created by RGAA on 2023/12/20.
//

#ifndef TC_APPLICATION_HTTP_HANDLER_H
#define TC_APPLICATION_HTTP_HANDLER_H

#include <nlohmann/json.hpp>
#include <asio2/asio2.hpp>

using namespace nlohmann;

namespace px
{

    class PxContext;
    class PxApplication;
    class PxRunGameManager;

    class HttpHandler {
    public:

        explicit HttpHandler(const std::shared_ptr<PxApplication>& app);

        void HandlePing(http::web_request &req, http::web_response &rep);
        void HandleSimpleInfo(http::web_request &req, http::web_response &rep);
        void HandleGames(http::web_request &req, http::web_response &rep);
        void HandleGameStart(http::web_request &req, http::web_response &rep);
        void HandleGameStop(http::web_request &req, http::web_response &rep);
        void HandleRunningGames(http::web_request &req, http::web_response &rep);
        void HandleStopServer(http::web_request &req, http::web_response &rep);
        void HandleAllRunningProcesses(http::web_request &req, http::web_response &rep);
        void HandleKillProcess(http::web_request &req, http::web_response &rep);
        void HandleResourcesFile(http::web_request &req, http::web_response &rep);
        void HandleSteamCacheFile(http::web_request &req, http::web_response &rep);

    private:
        std::string GetInstalledGamesAsJson();
        std::string WrapBasicInfo(int code, const std::string& msg, const std::string& data);
        std::string WrapBasicInfo(int code, const std::string& msg, const json& data);

    private:
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<PxApplication> app_ = nullptr;
        std::shared_ptr<PxRunGameManager> run_game_mgr_ = nullptr;

    };

}

#endif //TC_APPLICATION_HTTP_HANDLER_H
