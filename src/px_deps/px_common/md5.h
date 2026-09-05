#ifndef PX_COMMON_NEW_MD5_H
#define PX_COMMON_NEW_MD5_H

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <openssl/evp.h>

namespace px {

class Md5Hasher final {
public:
    Md5Hasher();

    Md5Hasher(const Md5Hasher&) = delete;
    Md5Hasher& operator=(const Md5Hasher&) = delete;
    Md5Hasher(Md5Hasher&&) noexcept = default;
    Md5Hasher& operator=(Md5Hasher&&) noexcept = default;

    void Update(std::span<const std::byte> bytes);
    void Update(std::string_view text);
    [[nodiscard]] std::string FinishHex();

private:
    using Context = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
    Context context_{nullptr, &EVP_MD_CTX_free};
    bool finished_{false};
};

class MD5 final {
public:
    [[nodiscard]] static std::string Hex(std::string_view input);
};

}  // namespace px

#endif  // PX_COMMON_NEW_MD5_H
