//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_H
#define GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_H

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace px_console
{

    class ConsoleUser;
    class ConsoleDevice;

    class ConsoleUserDevice {
    public:
        // parse single json return value
        static std::shared_ptr<ConsoleUserDevice> FromJson(const std::string& body);
        static std::shared_ptr<ConsoleUserDevice> FromObj(const json& obj);
        std::string Dump();

    public:
        std::string uid_;
        std::string device_id_;
        int64_t created_ts_;
        std::string created_ts_readable_;
        std::shared_ptr<ConsoleUser> user_ = nullptr;
        std::shared_ptr<ConsoleDevice> device_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_CONSOLE_USER_DEVICE_H
