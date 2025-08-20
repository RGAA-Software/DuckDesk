//
// Created by RGAA on 19/08/2025.
//

#include "gr_spvr_database.h"
#include "gr_device.h"
#include "tc_common_new/log.h"
#include "tc_common_new/uuid.h"
#include "tc_common_new/md5.h"
#include "tc_common_new/time_util.h"
#include "spvr/gr_spvr_settings.h"

namespace tc
{

    GrSpvrDatabase::GrSpvrDatabase(const std::shared_ptr<GrSpvrContext>& ctx) {
        context_ = ctx;
    }

    bool GrSpvrDatabase::Init() {
        auto settings = GrSpvrSettings::Instance();
        try {
            mgo_instance_ = std::make_shared<mongocxx::instance>();
            mongocxx::uri uri(settings->mongo_url_);
            mgo_client_ = std::make_shared<mongocxx::client>(uri);
            mgo_db_ = mgo_client_->database(settings->mongo_db_name_);
            c_device_ = mgo_db_.collection("gr_device");

            PingDatabase();

        } catch(std::exception& e) {
            LOGE("mongodb init failed: {} {}", settings->mongo_url_, settings->mongo_db_name_);
            return false;
        }
        LOGI("Connect to mongodb success: {} {} ", settings->mongo_url_, settings->mongo_db_name_);
        return true;
    }

    void GrSpvrDatabase::PingDatabase() {
        // check connection
        // throws an exception when failed
        mgo_db_.run_command(bsoncxx::builder::basic::make_document(
            bsoncxx::builder::basic::kvp("ping", 1)
        ));
    }

    std::shared_ptr<GrDevice> GrSpvrDatabase::GenerateNewDevice(const std::string& req_info, const std::string& platform) {
        std::shared_ptr<GrDevice> the_device = nullptr;
        bool ignore_req_info = false;
        while (true) {
            std::string seed;
            if (!req_info.empty() && !ignore_req_info) {
                seed = req_info;
            } else {
                seed = GetUUID();
            }

            auto md5_str = MD5::Hex(seed);
            std::stringstream ss;
            ss
                << md5_str[0] % 10
                << md5_str[7] % 10
                << md5_str[11] % 10
                << md5_str[16] % 10
                << md5_str[18] % 10
                << md5_str[23] % 10
                << md5_str[26] % 10
                << md5_str[28] % 10
                << md5_str[30] % 10;
            auto new_client_id = ss.str();
            auto device = FindDeviceByDeviceIdAndGeneratedSeed(new_client_id, seed);
            if (device) {
                auto new_random_pwd = GenerateRandomPassword();
                // update new random password
                LOGI("Before update device.");
                device->random_pwd_ = MD5::Hex(new_random_pwd);
                this->UpdateDevice(device->device_id_, {
                    {kSpvrDeviceRandomPwd, device->random_pwd_}
                });
                LOGI("After update device.");
                // recover to clear pwd, then -> client
                the_device = std::move(device);
                the_device->random_pwd_ = new_random_pwd;
            }
            else {
                device= FindDeviceByDeviceId(new_client_id);
                if (device) {
                    ignore_req_info = true;
                    LOGW("We found the same final id, but the seed is not equal, regenerate one.");
                    continue;
                }
                else {
                    // insert device
                    auto new_random_pwd = GenerateRandomPassword();
                    auto new_device = std::make_shared<GrDevice>();
                    new_device->device_id_ = new_client_id;
                    new_device->seed_ = seed;
                    new_device->random_pwd_ = MD5::Hex(new_random_pwd);
                    new_device->deleted_ = 0;
                    new_device->created_timestamp_ = (int64_t) TimeUtil::GetCurrentTimestamp();
                    new_device->updated_timestamp_ = (int64_t) TimeUtil::GetCurrentTimestamp();
                    new_device->platform_ = platform;
                    InsertDevice(new_device);

                    new_device->random_pwd_ = new_random_pwd;
                    the_device = new_device;
                    LOGI("Generate a new device id: {}, seed: {}", the_device->device_id_, seed);
                }
            }
            break;
        }
        return the_device;
    }

    bool GrSpvrDatabase::InsertDevice(const std::shared_ptr<GrDevice>& device) {
        auto doc = device->AsBsonDocument();
        try {
            c_device_.insert_one(doc.view());
            return true;
        } catch(std::exception& e) {
            LOGE("Insert failed:{}", e.what());
            return false;
        }
    }

    bool GrSpvrDatabase::UpdateDevice(const std::string& device_id, const std::map<std::string, std::string>& info) {
        auto update_doc = bsoncxx::builder::basic::document{};
        for (const auto& [k, v] : info) {
            update_doc.append(kvp(k, v));
        }
        update_doc.append(kvp(kSpvrDeviceUpdatedTimestamp, (int64_t)(TimeUtil::GetCurrentTimestamp())));

        auto r = c_device_.update_one(
            make_document(kvp(kSpvrDeviceId, device_id)),
            make_document(kvp("$set", update_doc.view()))
        );

        return r.has_value();
    }

    std::shared_ptr<GrDevice> GrSpvrDatabase::FindDeviceByDeviceId(const std::string& device_id) {
        auto result = c_device_.find_one(make_document(
            kvp(kSpvrDeviceId, device_id)
        ));
        if (!result.has_value()) {
            return nullptr;
        }
        auto val = result.value();
        auto device = std::make_shared<GrDevice>();
        device->ParseFrom(val);
        return device;
    }

    std::shared_ptr<GrDevice> GrSpvrDatabase::FindDeviceByDeviceIdAndGeneratedSeed(const std::string& device_id, const std::string& seed) {
        auto result = c_device_.find_one(make_document(
            kvp(kSpvrDeviceId, device_id),
            kvp(kSpvrDeviceSeed, seed)
        ));
        if (!result.has_value()) {
            return nullptr;
        }
        auto val = result.value();
        auto device = std::make_shared<GrDevice>();
        device->ParseFrom(val);
        return device;
    }

    std::string GrSpvrDatabase::GenerateRandomPassword() {
        std::string random_string;
        std::random_device rd;
        std::mt19937 rng(rd());
        std::uniform_int_distribution<int> distribution(0, 61);
        for (int i = 0; i < 8; ++i) {
            int random_num = distribution(rng);
            char random_char;
            if (random_num < 26) {
                random_char = 'A' + random_num;
            } else if (random_num < 52) {
                random_char = 'a' + random_num - 26;
            } else {
                random_char = '0' + random_num - 52;
            }
            random_string.push_back(random_char);
        }
        return random_string;
    }
    
}