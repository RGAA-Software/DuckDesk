//
// Created by RGAA on 20/08/2025.
//

#ifndef GAMMARAYPREMIUM_GR_HTTP_RESPONSE_H
#define GAMMARAYPREMIUM_GR_HTTP_RESPONSE_H

#include <drogon/HttpResponse.h>
#include "gr_http_code.h"
#include "gr_json_parser.h"

using namespace drogon;

namespace tc
{

    static HttpResponsePtr MakeResp(int status_code, const Json::Value& data) {
        Json::Value value;
        value["data"] = data;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode((HttpStatusCode)status_code);
        return resp;
    }

    static HttpResponsePtr MakeResp(int status_code) {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode((HttpStatusCode)status_code);
        return resp;
    }

    static HttpResponsePtr MakeOkResp() {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode(HttpStatusCode::k200OK);
        return resp;
    }

    static HttpResponsePtr MakeOkResp(const Json::Value& data) {
        Json::Value value;
        value["data"] = data;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode(HttpStatusCode::k200OK);
        return resp;
    }

    static HttpResponsePtr Make404Resp() {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode(HttpStatusCode::k404NotFound);
        return resp;
    }

    static HttpResponsePtr MakeErrParamResp() {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode((HttpStatusCode)kErrInvalidParam);
        return resp;
    }

    static HttpResponsePtr MakeErrNotFoundInDatabaseResp() {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode((HttpStatusCode)kErrNotFoundInDatabase);
        return resp;
    }

    static HttpResponsePtr MakeErrOperateDatabaseResp() {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode((HttpStatusCode)kErrOperateDatabaseFailed);
        return resp;
    }

    static HttpResponsePtr MakeErAppkeyResp() {
        Json::Value value;
        auto resp = HttpResponse::newHttpJsonResponse(value);
        resp->setStatusCode((HttpStatusCode)kErrInvalidAppkey);
        return resp;
    }

}

#endif //GAMMARAYPREMIUM_GR_HTTP_RESPONSE_H
