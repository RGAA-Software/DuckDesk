//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_UUID_H
#define TC_APPLICATION_UUID_H

#include <string>
#include "md5.h"
#include "uuid_impl/uuid_impl.hpp"

#ifdef WIN32
#pragma comment(lib, "Bcrypt.lib")
#endif

namespace tc
{
    static std::string GetUUID() {
        tc::uuid uuid;
        return uuid.generate().short_uuid(32);
    }

    static std::string GetUUIDInMD5() {
        return MD5::Hex(GetUUID());
    }
}

#endif //TC_APPLICATION_UUID_H
