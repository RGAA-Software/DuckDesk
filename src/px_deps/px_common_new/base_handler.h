//
// Created by RGAA on 26/02/2025.
//

#ifndef GAMMARAYSERVER_BASE_HANDLER_H
#define GAMMARAYSERVER_BASE_HANDLER_H

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>
#include <asio2/asio2.hpp>

using namespace nlohmann;

namespace tc
{

    constexpr auto kHandlerErrParams = 600;
    constexpr auto kHandlerErrBody = 601;

    class BaseHandler {
    public:
        virtual void RegisterPaths() {}
        virtual std::string GetErrorMessage(int code);

        std::unordered_map<std::string, std::string> GetQueryParams(std::string_view query);
        std::unordered_map<std::string, std::string> GetQueryParams(const std::string& query);
        std::optional<std::string> GetParam(const std::unordered_map<std::string, std::string>& params, const std::string& key);

        std::string WrapBasicInfo(int code, const std::string& msg, const std::string& data);
        std::string WrapBasicInfo(int code, const std::string& msg, const nlohmann::json& data);

        void SendBackJson(http::web_response& resp, int code, const std::string& msg, const std::string& data);
        void SendBackKnownJson(http::web_response& resp, int code, const std::string& data);
        void SendOkJson(http::web_response& resp, const std::string& data);
        void SendErrorJson(http::web_response& resp, int code);
        void SendBackJson(http::web_response& resp, int code, const std::string& msg, const nlohmann::json& data);


    };

}

#endif //GAMMARAYSERVER_BASE_HANDLER_H
