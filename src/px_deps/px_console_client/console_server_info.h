//
// Created by RGAA on 26/03/2025.
//

#ifndef PX_CONSOLE_SERVER_INFO_H
#define PX_CONSOLE_SERVER_INFO_H

#include <string>
#include <vector>

namespace px_console
{

    // Relay Server Information
    class ConsoleRelayServerInfo {
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
    class ConsoleProfileServerInfo {
    public:
        std::string srv_type_;
        std::string srv_name_;
        std::string srv_id_;
        std::string srv_w3c_ip_;
        std::string srv_local_ip_;
        std::string srv_working_port_;
        std::string srv_grpc_port_;
    };

    class ConsoleOnlineServers {
    public:
        std::vector<ConsoleProfileServerInfo> pr_servers_;
        std::vector<ConsoleRelayServerInfo> relay_servers_;
    };

}

#endif //PX_CONSOLE_SERVER_INFO_H
