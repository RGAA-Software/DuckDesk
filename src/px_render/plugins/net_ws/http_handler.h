//
// Created by RGAA on 2023/12/20.
//

#ifndef TC_APPLICATION_HTTP_HANDLER_H
#define TC_APPLICATION_HTTP_HANDLER_H

#include <asio2/asio2.hpp>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include "px_common_new/base_handler.h"
#include "direct_session_grant_store.h"

using namespace nlohmann;

namespace px
{

    class WsPlugin;

    class HttpHandler : public BaseHandler {
    public:
        explicit HttpHandler(WsPlugin* plugin);
        std::string GetErrorMessage(int code) override;

        // /api/ping
        void HandlePing(http::web_request &req, http::web_response &resp);

        // /verify/security/password
        void HandleVerifySecurityPassword(
            const std::shared_ptr<asio2::http_session>& session,
            http::web_request& req,
            http::web_response& resp);

        // /get/render/configuration
        void HandleGetRenderConfiguration(http::web_request& req, http::web_response& resp);

        // /panel/stream/message
        void HandlePanelStreamMessage(http::web_request& req, http::web_response& resp);

        // /alloc/local/rtc
        void HandleAllocLocalRtc(std::shared_ptr<asio2::http_session> &session_ptr, http::web_request& req, http::web_response& resp);
    private:
        // 校验安全密码(md5)，逻辑与 /verify/security/password 一致：设备未设置安全密码时视为通过
        bool VerifySafetyPassword(const std::unordered_map<std::string, std::string>& params);
        void CloseAdmittedLogicalSessionBinding(const std::string& logical_session_id,
                                                const std::string& binding_id);
    private:
        WsPlugin* plugin_ = nullptr;
        DirectSessionGrantStore direct_session_grants_;

    };

}

#endif //TC_APPLICATION_HTTP_HANDLER_H
