//
// Created by RGAA on 28/11/2025.
//

#ifndef PIXELSPREMIUM_CONSOLE_USER_DEVICE_H
#define PIXELSPREMIUM_CONSOLE_USER_DEVICE_H

#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace px_console {

class ConsoleUserDevice {
public:
    // parse single json return value
    static std::shared_ptr<ConsoleUserDevice> FromJson(const std::string& body);
    static std::shared_ptr<ConsoleUserDevice> FromObj(const nlohmann::json& device_profile);
    std::string Dump();

    std::string device_id_{};
    std::string public_code_{};
    std::string device_name_{};
    std::string platform_{};
    bool disabled_{};
    std::int64_t revision_{};
    std::string registered_at_{};
};

}  // namespace px_console

#endif  // PIXELSPREMIUM_CONSOLE_USER_DEVICE_H
