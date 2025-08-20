//
// Created by RGAA on 20/08/2025.
//

#include "gr_device.h"

namespace tc
{

    bsoncxx::document::value GrDevice::AsBsonDocument() {
        return make_document(
            kvp(kSpvrDeviceId, device_id_),
            kvp(kSpvrDeviceBelongToUser, belong_to_user_),
            kvp(kSpvrDeviceSeed, seed_),
            kvp(kSpvrDeviceRandomPwd, random_pwd_),
            kvp(kSpvrDeviceSafetyPwd, safety_pwd_),
            kvp(kSpvrDeviceDeleted, deleted_),
            kvp(kSpvrDeviceCreatedTimestamp, created_timestamp_),
            kvp(kSpvrDeviceUpdatedTimestamp, updated_timestamp_),
            kvp(kSpvrUsedTime, used_time_),
            kvp(kSpvrPlatform, platform_)
        );
    }

    bool GrDevice::ParseFrom(const bsoncxx::document::value& val) {
        try {
            auto bson = val.view();
            this->obj_id_ = bson["_id"].get_oid().value.to_string();
            this->device_id_ = std::string{bson[kSpvrDeviceId].get_string().value};
            this->belong_to_user_ = std::string{bson[kSpvrDeviceBelongToUser].get_string().value};
            this->seed_ = std::string{bson[kSpvrDeviceSeed].get_string().value};
            this->random_pwd_ = std::string{bson[kSpvrDeviceRandomPwd].get_string().value};
            this->safety_pwd_ = std::string{bson[kSpvrDeviceSafetyPwd].get_string().value};
            this->deleted_ = bson[kSpvrDeviceDeleted].get_int32();
            this->created_timestamp_ = bson[kSpvrDeviceCreatedTimestamp].get_int64();
            this->updated_timestamp_ = bson[kSpvrDeviceUpdatedTimestamp].get_int64();
            this->used_time_ = bson[kSpvrUsedTime].get_int64();
            this->platform_ = std::string{bson[kSpvrPlatform].get_string().value};
            return true;
        }
        catch(std::exception& e) {
            return false;
        }
    }

}