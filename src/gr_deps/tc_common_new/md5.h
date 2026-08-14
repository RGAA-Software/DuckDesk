//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_MD5_H
#define TC_APPLICATION_MD5_H

#include "string_util.h"
#define OPENSSL_API_COMPAT 10100
#include <openssl/md5.h>

namespace tc
{

    class MD5 {
    public:

        static std::string Hex(const std::string& input) {
            if (input.empty()) {
                return "";
            }

            unsigned char digest[MD5_DIGEST_LENGTH]; // MD5_DIGEST_LENGTH = 16
            ::MD5(reinterpret_cast<const unsigned char*>(input.c_str()), input.size(), digest);

            std::ostringstream oss;
            oss << std::hex << std::setfill('0');
            for (int i = 0; i < MD5_DIGEST_LENGTH; ++i) {
                oss << std::setw(2) << static_cast<int>(digest[i]);
            }
            return oss.str();
        }

        //static std::string Hex(const std::string& input) {
        //    if (input.empty()) {
        //        return "";
        //    }
        //    return StringUtil::ToLowerCpy(asio2::md5(input).str());
        //}

    };

}

#endif //TC_APPLICATION_MD5_H
