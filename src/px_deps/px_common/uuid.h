//
// Created by RGAA on 2023-12-17.
//

#ifndef TC_APPLICATION_UUID_H
#define TC_APPLICATION_UUID_H

#include <string>
#include <string_view>

#include "md5.h"
#include "uuid_impl/uuid_impl.hpp"

#ifdef WIN32
#pragma comment(lib, "Bcrypt.lib")
#endif

namespace px {

// An opaque 32-character identifier using the Base64 alphabet, not a UUID.
// It can contain '+' and '/'; do not use it as a filename or a typed UUID field.
inline std::string GenerateRandomBase64Id() {
    px::uuid random_generator{};
    return random_generator.generate().short_uuid(32);
}

inline std::string GenerateRandomBase64IdMd5() { return MD5::Hex(GenerateRandomBase64Id()); }

// Canonical UUID text for UUID-typed protocol fields and filesystem identifiers.
inline std::string GetCanonicalUUID() {
    px::uuid generated_uuid{};
    return generated_uuid.generate().str();
}

inline bool IsCanonicalUUID(const std::string_view identifier) noexcept {
    if (identifier.size() != 36) {
        return false;
    }
    for (std::size_t character_index{}; character_index < identifier.size(); ++character_index) {
        const char character{identifier[character_index]};
        if (character_index == 8 || character_index == 13 || character_index == 18 || character_index == 23) {
            if (character != '-') {
                return false;
            }
        } else if (!((character >= '0' && character <= '9') || (character >= 'a' && character <= 'f') || (character >= 'A' && character <= 'F'))) {
            return false;
        }
    }
    return true;
}

}  // namespace px

#endif  // TC_APPLICATION_UUID_H
