//
// Created by RGAA on 26/03/2025.
//

#ifndef GAMMARAY_SPVR_SERVER_INFO_H
#define GAMMARAY_SPVR_SERVER_INFO_H

#include <string>
#include <vector>

namespace spvr
{

    // Relay Server Information
    class SpvrRelayServerInfo {
    public:
        std::string srv_type_;
        std::string srv_name_;
        std::string srv_id_;
        std::string srv_w3c_ip_;
        std::string srv_local_ip_;
        std::string srv_working_port_;
        std::string srv_grpc_port_;
    };

    // Profile Server Information
    class SpvrProfileServerInfo {
    public:
        std::string srv_type_;
        std::string srv_name_;
        std::string srv_id_;
        std::string srv_w3c_ip_;
        std::string srv_local_ip_;
        std::string srv_working_port_;
        std::string srv_grpc_port_;
    };

    class SpvrOnlineServers {
    public:
        std::vector<SpvrProfileServerInfo> pr_servers_;
        std::vector<SpvrRelayServerInfo> relay_servers_;
    };

}

#endif //GAMMARAY_SPVR_SERVER_INFO_H
