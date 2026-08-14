//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_SPVR_USER_DEVICE_H
#define GAMMARAYPREMIUM_SPVR_USER_DEVICE_H

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace spvr
{

    class SpvrUser;
    class SpvrDevice;

    class SpvrUserDevice {
    public:
        // parse single json return value
        static std::shared_ptr<SpvrUserDevice> FromJson(const std::string& body);
        static std::shared_ptr<SpvrUserDevice> FromObj(const json& obj);
        std::string Dump();

    public:
        std::string uid_;
        std::string device_id_;
        int64_t created_ts_;
        std::string created_ts_readable_;
        std::shared_ptr<SpvrUser> user_ = nullptr;
        std::shared_ptr<SpvrDevice> device_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_SPVR_USER_DEVICE_H
