//
// Created by RGAA on 2024-04-01.
//

#ifndef TC_SERVER_STEAM_NET_RESP_H
#define TC_SERVER_STEAM_NET_RESP_H

#include <nlohmann/json.hpp>
using namespace nlohmann;

namespace tc
{

    class NetResp {
    public:

        static NetResp Make(int code, const std::string& msg, const std::string& data) {
            return {code, msg, data};
        }

        NetResp() {}

        NetResp(int code, const std::string& msg, const std::string& data) {
            this->code_ = code;
            this->msg_ = msg;
            this->data_ = data;
        }

        std::string Dump() {
            json obj;
            obj["code"] = this->code_;
            obj["msg"] = this->msg_;
            obj["data"] = this->data_;
            return obj.dump(2);
        }

    private:

        int code_{0};
        std::string msg_;
        std::string data_;
    };

}

#endif //TC_SERVER_STEAM_NET_RESP_H
