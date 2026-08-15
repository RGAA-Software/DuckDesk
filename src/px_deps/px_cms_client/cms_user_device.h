//
// Created by RGAA on 28/11/2025.
//

#ifndef GAMMARAYPREMIUM_CMS_USER_DEVICE_H
#define GAMMARAYPREMIUM_CMS_USER_DEVICE_H

#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using namespace nlohmann;

namespace px_cms
{

    class CmsUser;
    class CmsDevice;

    class CmsUserDevice {
    public:
        // parse single json return value
        static std::shared_ptr<CmsUserDevice> FromJson(const std::string& body);
        static std::shared_ptr<CmsUserDevice> FromObj(const json& obj);
        std::string Dump();

    public:
        std::string uid_;
        std::string device_id_;
        int64_t created_ts_;
        std::string created_ts_readable_;
        std::shared_ptr<CmsUser> user_ = nullptr;
        std::shared_ptr<CmsDevice> device_ = nullptr;
    };

}

#endif //GAMMARAYPREMIUM_CMS_USER_DEVICE_H
