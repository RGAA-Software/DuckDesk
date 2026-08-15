#ifndef BASE64_H
#define BASE64_H

#include <string>
#include <cstdint>

namespace tc
{

    class Base64 {
    public:
        static std::string Base64Encode(const uint8_t* data, std::size_t len);
        static std::string Base64Encode(const std::string& s);
        static std::string Base64Decode(const std::string& data);
    };

}
#endif // BASE64_H
