#ifndef FIREWALL_MANAGER_H
#define FIREWALL_MANAGER_H

#include <string>
#include <mutex>
#include <utility>

struct INetFwPolicy2;
struct INetFwRules;

namespace tc
{

    class RulesInfo {
    public:
        std::string name; // rule name
        std::string program_path; // app path
        std::string desc; // description
        int type; // 1=in or 2=out
        int is_allow; // 1=allow or 2=forbidden
        int enable; // 1=enable or 0=close
        int interface_type; //

        RulesInfo(std::string n, std::string path, std::string desc = "", int type = 1)
                : name(std::move(n)), program_path(std::move(path)), desc(std::move(desc)), type(type) {
            is_allow = 1;
            enable = 1;
            interface_type = 0;
        }
    };

    class FirewallHelper {
    public:
        static FirewallHelper *Instance();

        bool AddProgramToFirewall(const RulesInfo& info);

        // add a port-based rule, e.g. local_ports = "60430-60490", protocol = 17(UDP) / 6(TCP), direction: 1=in or 2=out
        bool AddPortToFirewall(const std::string& rule_name, const std::string& local_ports, int protocol, int direction = 1);

        bool RemoveProgramFromFirewall(const std::string &rule_name);

    private:
        FirewallHelper();

        ~FirewallHelper();

    private:
        bool is_init = false;
        long hr = -1;
        INetFwPolicy2 *fw_policy2 = nullptr;
        INetFwRules *fw_rules = nullptr;
        std::mutex lock_mutex;

    };

}

#endif
