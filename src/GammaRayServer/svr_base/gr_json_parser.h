//
// Created by RGAA on 20/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_JSON_PARSER_H
#define GAMMARAYPREMIUM_GR_JSON_PARSER_H

#include <json/json.h>
#include "tc_3rdparty/expt/expected.h"
#include "tc_common_new/log.h"

namespace tc
{

    static Result<Json::Value, bool> ParseJsonFromString(const std::string& json) {
        if (json.empty()) {
            return false;
        }

        JSONCPP_STRING err;
        Json::Value value;
        auto r = std::unique_ptr<Json::CharReader>(Json::CharReaderBuilder().newCharReader())
                ->parse(json.data(), json.data() + json.size(), &value, &err) && err.empty();
        if (r && !value.empty()) {
            return value;
        }
        LOGE("Parse json failed: {}", err);
        return TcErr(false);
    }

}

#endif //GAMMARAYPREMIUM_GR_JSON_PARSER_H
