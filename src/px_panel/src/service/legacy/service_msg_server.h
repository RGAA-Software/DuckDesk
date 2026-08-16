//
// Created by RGAA on 29/11/2024.
//

#ifndef PX_SERVICE_MSG_SERVER_H
#define PX_SERVICE_MSG_SERVER_H

#include <string>
#include <asio2/websocket/ws_server.hpp>
#include <asio2/asio2.hpp>
#include "px_common_new/concurrent_hashmap.h"

namespace px
{

    class PxService;
    class ServiceContext;
    class RenderManager;

    class SessionWrapper {
    public:
        uint64_t socket_fd_ = 0;
        std::shared_ptr<asio2::ws_session> session_;
        std::string from_ = "";
    };

    class ServiceMsgServer {
    public:
        explicit ServiceMsgServer(const std::shared_ptr<ServiceContext>& context, const std::shared_ptr<RenderManager>& rm);
        void Init(const std::shared_ptr<PxService>& service);
        void Start();
        void ParseMessage(const std::shared_ptr<SessionWrapper>& sw, std::string_view data);
        void PostBinaryMessage(const std::string& msg);

    private:
        void ProcessStartRender(const std::string& work_dir, const std::string& app_path, const std::vector<std::string>& args);
        void ProcessStopRender();
        void ProcessRestartRender(const std::string& work_dir, const std::string& app_path, const std::vector<std::string>& args);
        void ProcessHeartBeat(int64_t index);
        void ProcessCtrlAltDelete() const;

    private:
        std::shared_ptr<RenderManager> render_manager_ = nullptr;
        std::shared_ptr<asio2::ws_server> server_ = nullptr;
        px::ConcurrentHashMap<uint64_t, std::shared_ptr<SessionWrapper>> sessions_;
        std::shared_ptr<ServiceContext> context_ = nullptr;
        std::shared_ptr<PxService> service_ = nullptr;
        std::string service_path_ = "/service/message";
    };

}

#endif //PX_SERVICE_MSG_SERVER_H
