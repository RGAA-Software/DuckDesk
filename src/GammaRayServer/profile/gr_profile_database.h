//
// Created by RGAA on 19/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_PROFILE_DATABASE_H
#define GAMMARAYPREMIUM_GR_PROFILE_DATABASE_H

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

    class GrProfileContext;

    class GrProfileDatabase {
    public:
        explicit GrProfileDatabase(const std::shared_ptr<GrProfileContext>& ctx);
        bool Init();

    private:
        std::shared_ptr<GrProfileContext> context_ = nullptr;
        std::shared_ptr<mongocxx::instance> mgo_instance_ = nullptr;
        std::shared_ptr<mongocxx::client> mgo_client_ = nullptr;
        mongocxx::database mgo_db_;
        mongocxx::collection c_profile_;

    };

}


#endif //GAMMARAYPREMIUM_GR_PROFILE_DATABASE_H
