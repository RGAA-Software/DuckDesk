#include "md5.h"

#include <array>
#include <format>
#include <stdexcept>

namespace px {
namespace {

std::string ToHex(std::span<const unsigned char> digest) {
    std::string result{};
    result.reserve(digest.size() * 2);
    for (const auto value : digest) result += std::format("{:02x}", static_cast<unsigned int>(value));
    return result;
}

}  // namespace

Md5Hasher::Md5Hasher() : context_(EVP_MD_CTX_new(), &EVP_MD_CTX_free) {
    if (!context_ || EVP_DigestInit_ex(context_.get(), EVP_md5(), nullptr) != 1) {
        throw std::runtime_error("failed to initialize MD5 digest context");
    }
}

void Md5Hasher::Update(const std::span<const std::byte> bytes) {
    if (finished_) throw std::logic_error("cannot update a finalized MD5 digest");
    if (!bytes.empty() && EVP_DigestUpdate(context_.get(), bytes.data(), bytes.size()) != 1) {
        throw std::runtime_error("failed to update MD5 digest");
    }
}

void Md5Hasher::Update(const std::string_view text) {
    Update(std::as_bytes(std::span{text}));
}

std::string Md5Hasher::FinishHex() {
    if (finished_) throw std::logic_error("MD5 digest was already finalized");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int size{0};
    if (EVP_DigestFinal_ex(context_.get(), digest.data(), &size) != 1) throw std::runtime_error("failed to finalize MD5 digest");
    finished_ = true;
    return ToHex(std::span<const unsigned char>{digest}.first(size));
}

std::string MD5::Hex(const std::string_view input) {
    if (input.empty()) return {};
    Md5Hasher hasher{};
    hasher.Update(input);
    return hasher.FinishHex();
}

}  // namespace px
