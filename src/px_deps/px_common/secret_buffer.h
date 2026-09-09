#pragma once

#include <openssl/crypto.h>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace px {

// Explicitly sensitive bytes: no streaming, implicit string conversion, copy or
// Debug helper. Serialization is allowed only at a named protected boundary.
class SecretBuffer final {
public:
    explicit SecretBuffer(std::string_view bytes) : bytes_(bytes.begin(), bytes.end()) {}
    ~SecretBuffer() { if (!bytes_.empty()) { OPENSSL_cleanse(bytes_.data(), bytes_.size()); } }
    SecretBuffer(const SecretBuffer&) = delete;
    SecretBuffer& operator=(const SecretBuffer&) = delete;
    [[nodiscard]] std::span<const char> Bytes() const noexcept { return bytes_; }
    [[nodiscard]] std::string_view View() const noexcept { return {bytes_.data(), bytes_.size()}; }
    [[nodiscard]] static std::shared_ptr<SecretBuffer> Take(std::string value) {
        auto result = std::make_shared<SecretBuffer>(value);
        if (!value.empty()) { OPENSSL_cleanse(value.data(), value.size()); }
        return result;
    }
private:
    std::vector<char> bytes_{};
};

} // namespace px
