//
// Created by RGAA on 2024/2/2.
//

#ifndef TC_APPLICATION_RESPONSE_H
#define TC_APPLICATION_RESPONSE_H

#include <string>
#include <cstdint>
#include <memory>

namespace tc
{

    template<typename E, typename V>
    class Response {
    public:
        static Response Make(const E& e, const V& v, const std::string& m) {
            Response resp;
            resp.ok_ = e;
            resp.value_ = v;
            resp.msg_ = m;
            return resp;
        }

        static Response Make(const E& e, const V& v) {
            return Make(e, v, "");
        }

        static std::shared_ptr<Response> MakePtr(const E& e, const V& v, const std::string& m) {
            auto resp = std::make_shared<Response>();
            resp->ok_ = e;
            resp->value_ = v;
            resp->msg_ = m;
            return resp;
        }

        static std::shared_ptr<Response> MakePtr(const E& e, const V& v) {
            return MakePtr(e, v, "");
        }

    public:
        E ok_;
        V value_;
        std::string msg_;
    };

    class ResponseBool : public Response<bool, int> {
    public:

    };

    using RespBoolBool = Response<bool, bool>;
}

#endif //TC_APPLICATION_RESPONSE_H
