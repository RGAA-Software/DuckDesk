//
// Created by RGAA on 19/08/2025.
//

#include "gr_profile_database.h"
#include "gr_profile_settings.h"
#include "tc_common_new/log.h"

namespace tc
{

    GrProfileDatabase::GrProfileDatabase(const std::shared_ptr<GrProfileContext>& ctx) {
        context_ = ctx;
    }

    bool GrProfileDatabase::Init() {
        auto settings = GrProfileSettings::Instance();
        try {
            mgo_instance_ = std::make_shared<mongocxx::instance>();
            mongocxx::uri uri(settings->mongo_url_);
            mgo_client_ = std::make_shared<mongocxx::client>(uri);
            mgo_db_ = mgo_client_->database(settings->mongo_db_name_);
            c_profile_ = mgo_db_.collection("gr_device");

            // check connection
            // throws an exception when failed
            mgo_db_.run_command(bsoncxx::builder::basic::make_document(
                bsoncxx::builder::basic::kvp("ping", 1)
            ));

        } catch(std::exception& e) {
            LOGE("mongodb init failed: {} {}", settings->mongo_url_, settings->mongo_db_name_);
            return false;
        }
        LOGI("Connect to mongodb success: {} {} ", settings->mongo_url_, settings->mongo_db_name_);
        return true;
    }

}