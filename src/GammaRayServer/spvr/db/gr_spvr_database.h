//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_SPVR_DATABASE_H
#define GAMMARAYPREMIUM_GR_SPVR_DATABASE_H

#include <bsoncxx/builder/basic/document.hpp>
#include <bsoncxx/builder/stream/document.hpp>
#include <bsoncxx/json.hpp>
#include <mongocxx/client.hpp>
#include <mongocxx/instance.hpp>
#include <mongocxx/uri.hpp>
using bsoncxx::builder::basic::kvp;
using bsoncxx::builder::basic::make_document;

namespace tc
{

    class GrDevice;
    class GrSpvrContext;

    class GrSpvrDatabase {
    public:
        explicit GrSpvrDatabase(const std::shared_ptr<GrSpvrContext>& ctx);
        bool Init();

        std::shared_ptr<GrDevice> GenerateNewDevice(const std::string& req_info, const std::string& platform);
        bool InsertDevice(const std::shared_ptr<GrDevice>& device);
        bool UpdateDevice(const std::string& device_id, const std::map<std::string, std::string>& info);
        std::shared_ptr<GrDevice> FindDeviceByDeviceId(const std::string& device_id);
        std::shared_ptr<GrDevice> FindDeviceByDeviceIdAndGeneratedSeed(const std::string& device_id, const std::string& seed);
        static std::string GenerateRandomPassword();

    private:
        void PingDatabase() noexcept(false);

    private:
        std::shared_ptr<GrSpvrContext> context_ = nullptr;
        std::shared_ptr<mongocxx::instance> mgo_instance_ = nullptr;
        std::shared_ptr<mongocxx::client> mgo_client_ = nullptr;
        mongocxx::database mgo_db_;
        mongocxx::collection c_device_;

    };

}


#endif //GAMMARAYPREMIUM_GR_PROFILE_DATABASE_H
