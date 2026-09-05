#include "px_aes.h"

#include <limits>
#include <memory>
#include <string_view>

#include <openssl/evp.h>

#include "log.h"

namespace px {
namespace {

using CipherContext = std::unique_ptr<EVP_CIPHER_CTX, decltype(&EVP_CIPHER_CTX_free)>;

bool ValidateArguments(std::span<const std::byte> input, std::span<const std::byte> key, std::span<const std::byte> iv, std::string_view operation) {
    if (input.empty() || input.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) || key.size() != kAes128KeySize ||
        iv.size() != kAes128IvSize) {
        LOGE("event=common.aes.invalid_argument operation={} input_size={} key_size={} iv_size={}", operation, input.size(), key.size(), iv.size());
        return false;
    }
    return true;
}

} // namespace

bool AesEncryptPcks7Cbc128(std::span<const std::byte> plaintext, std::span<const std::byte> key, std::span<const std::byte> iv,
                           std::vector<unsigned char>& ciphertext) {
    ciphertext.clear();
    if (!ValidateArguments(plaintext, key, iv, "encrypt")) {
        return false;
    }

    CipherContext context{EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free};
    if (!context) {
        LOGE("event=common.aes.context_create_failed operation=encrypt");
        return false;
    }

    if (EVP_EncryptInit_ex(context.get(), EVP_aes_128_cbc(), nullptr, reinterpret_cast<const unsigned char*>(key.data()),
                           reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        LOGE("event=common.aes.init_failed operation=encrypt");
        return false;
    }

    const int block_size = EVP_CIPHER_block_size(EVP_aes_128_cbc());
    ciphertext.resize(plaintext.size() + static_cast<std::size_t>(block_size));

    int update_size{};
    if (EVP_EncryptUpdate(context.get(), ciphertext.data(), &update_size, reinterpret_cast<const unsigned char*>(plaintext.data()),
                          static_cast<int>(plaintext.size())) != 1) {
        ciphertext.clear();
        LOGE("event=common.aes.update_failed operation=encrypt");
        return false;
    }

    int final_size{};
    if (EVP_EncryptFinal_ex(context.get(), ciphertext.data() + update_size, &final_size) != 1) {
        ciphertext.clear();
        LOGE("event=common.aes.finalize_failed operation=encrypt");
        return false;
    }

    ciphertext.resize(static_cast<std::size_t>(update_size + final_size));
    return true;
}

bool AesDecryptPcks7Cbc128(std::span<const std::byte> ciphertext, std::span<const std::byte> key, std::span<const std::byte> iv,
                           std::vector<unsigned char>& plaintext) {
    plaintext.clear();
    if (!ValidateArguments(ciphertext, key, iv, "decrypt")) {
        return false;
    }

    CipherContext context{EVP_CIPHER_CTX_new(), &EVP_CIPHER_CTX_free};
    if (!context) {
        LOGE("event=common.aes.context_create_failed operation=decrypt");
        return false;
    }

    if (EVP_DecryptInit_ex(context.get(), EVP_aes_128_cbc(), nullptr, reinterpret_cast<const unsigned char*>(key.data()),
                           reinterpret_cast<const unsigned char*>(iv.data())) != 1) {
        LOGE("event=common.aes.init_failed operation=decrypt");
        return false;
    }

    plaintext.resize(ciphertext.size());
    int update_size{};
    if (EVP_DecryptUpdate(context.get(), plaintext.data(), &update_size, reinterpret_cast<const unsigned char*>(ciphertext.data()),
                          static_cast<int>(ciphertext.size())) != 1) {
        plaintext.clear();
        LOGE("event=common.aes.update_failed operation=decrypt");
        return false;
    }

    int final_size{};
    if (EVP_DecryptFinal_ex(context.get(), plaintext.data() + update_size, &final_size) != 1) {
        plaintext.clear();
        LOGE("event=common.aes.finalize_failed operation=decrypt");
        return false;
    }

    plaintext.resize(static_cast<std::size_t>(update_size + final_size));
    return true;
}

} // namespace px
