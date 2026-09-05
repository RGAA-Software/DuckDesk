#ifndef BASE64_H
#define BASE64_H

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace px
{

    class Base64 {
    public:
        static std::string Base64Encode(std::span<const std::uint8_t> data);
        static std::string Base64Encode(std::string_view text);
        static std::string Base64Decode(std::string_view data);
    };

}
#endif // BASE64_H
