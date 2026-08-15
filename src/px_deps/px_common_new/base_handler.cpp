//
// Created by RGAA on 26/02/2025.
//

#include "base_handler.h"
#include "url_helper.h"

namespace tc
{

    std::string BaseHandler::GetErrorMessage(int code) {
        if (code == 200) {
            return "ok";
        }
        else if (code == kHandlerErrParams) {
            return "request error params";
        }
        else if (code == kHandlerErrBody) {
            return "request error body";
        }
        return "unknown code: " + std::to_string(code);
    }

    std::unordered_map<std::string, std::string> BaseHandler::GetQueryParams(std::string_view query) {
        std::string cpy_query(query.data(), query.size());
        return this->GetQueryParams(cpy_query);
    }

    std::unordered_map<std::string, std::string> BaseHandler::GetQueryParams(const std::string &query) {
        auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));
        return params;
    }

    std::optional<std::string> BaseHandler::GetParam(const std::unordered_map<std::string, std::string>& params, const std::string& key) {
        if (params.contains(key)) {
            return params.at(key);
        }
        return std::nullopt;
    }

    std::string BaseHandler::WrapBasicInfo(int code, const std::string& msg, const std::string& data) {
        json obj;
        obj["code"] = code;
        obj["message"] = msg;
        try {
            obj["data"] = json::parse(data);
        } catch(...) {
            obj["data"] = data;
        }
        return obj.dump();
    }

    std::string BaseHandler::WrapBasicInfo(int code, const std::string& msg, const json& data) {
        json obj;
        obj["code"] = code;
        obj["message"] = msg;
        obj["data"] = data;
        return obj.dump();
    }

    void BaseHandler::SendBackJson(http::web_response& resp, int code, const std::string& msg, const std::string& data) {
        auto target_msg = WrapBasicInfo(code, msg, data);
        resp.fill_json(target_msg);
    }

    void BaseHandler::SendBackKnownJson(http::web_response& resp, int code, const std::string& data) {
        auto target_msg = WrapBasicInfo(code, GetErrorMessage(code), data);
        resp.fill_json(target_msg);
    }

    void BaseHandler::SendOkJson(http::web_response& resp, const std::string& data) {
        this->SendBackKnownJson(resp, 200, data);
    }

    void BaseHandler::SendErrorJson(http::web_response& resp, int code) {
        this->SendBackKnownJson(resp, code, GetErrorMessage(code));
    }

    void BaseHandler::SendBackJson(http::web_response& resp, int code, const std::string& msg, const nlohmann::json& data) {
        auto target_msg = WrapBasicInfo(code, msg, data);
        resp.fill_json(target_msg);
    }
}
