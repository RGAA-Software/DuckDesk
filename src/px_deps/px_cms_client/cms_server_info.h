//
// Created by RGAA on 26/03/2025.
//

#ifndef GAMMARAY_CMS_SERVER_INFO_H
#define GAMMARAY_CMS_SERVER_INFO_H

#include <string>
#include <vector>

namespace px_cms
{

    // Relay Server Information
    class CmsRelayServerInfo {
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
    class CmsProfileServerInfo {
    public:
        std::string srv_type_;
        std::string srv_name_;
        std::string srv_id_;
        std::string srv_w3c_ip_;
        std::string srv_local_ip_;
        std::string srv_working_port_;
        std::string srv_grpc_port_;
    };

    class CmsOnlineServers {
    public:
        std::vector<CmsProfileServerInfo> pr_servers_;
        std::vector<CmsRelayServerInfo> relay_servers_;
    };

}

#endif //GAMMARAY_CMS_SERVER_INFO_H
