#include "base64.h"

#include "base64_impl/base64_impl.hpp"

namespace px
{

    std::string Base64::Base64Encode(std::span<const std::uint8_t> data) {
        return cereal::base64::encode(data.data(), data.size());
    }

    std::string Base64::Base64Encode(std::string_view text) {
        return Base64Encode(std::span<const std::uint8_t>{reinterpret_cast<const std::uint8_t*>(text.data()), text.size()});
    }

    std::string Base64::Base64Decode(std::string_view data) {
        return cereal::base64::decode(std::string{data});
    }

}
