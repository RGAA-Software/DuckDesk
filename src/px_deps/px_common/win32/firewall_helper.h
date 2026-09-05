#ifndef FIREWALL_MANAGER_H
#define FIREWALL_MANAGER_H

#include <string>
#include <utility>

namespace px {

struct RulesInfo final {
    std::string name{};
    std::string program_path{};
    std::string desc{};
    int type{1};
    int is_allow{1};
    int enable{1};
    int interface_type{};

    RulesInfo(std::string rule_name, std::string path, std::string description = {}, int rule_type = 1)
        : name(std::move(rule_name)), program_path(std::move(path)), desc(std::move(description)), type(rule_type) {}
};

class FirewallHelper final {
public:
    static bool AddProgramToFirewall(const RulesInfo& info);
    static bool AddPortToFirewall(const std::string& rule_name, const std::string& local_ports, int protocol, int direction = 1);
    static bool RemoveProgramFromFirewall(const std::string& rule_name);
};

}  // namespace px

#endif  // FIREWALL_MANAGER_H
