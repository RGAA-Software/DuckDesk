//
// Created by RGAA on 20/09/2025.
//

#include "auth_aes.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <string>
#include <vector>
#include <stdexcept>
#include <iostream>

// ===== Base64 编解码 =====
static std::string base64_encode(const std::vector<unsigned char>& data) {
    BIO* bio, * b64;
    BUF_MEM* bufferPtr;
    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bio);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL); // 不要换行
    BIO_write(b64, data.data(), data.size());
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bufferPtr);

    std::string result(bufferPtr->data, bufferPtr->length);
    BIO_free_all(b64);
    return result;
}

static std::vector<unsigned char> base64_decode(const std::string& input) {
    BIO* bio, * b64;
    int decodeLen = input.size();
    std::vector<unsigned char> buffer(decodeLen);

    b64 = BIO_new(BIO_f_base64());
    bio = BIO_new_mem_buf(input.data(), input.size());
    bio = BIO_push(b64, bio);
    BIO_set_flags(bio, BIO_FLAGS_BASE64_NO_NL);

    int length = BIO_read(bio, buffer.data(), input.size());
    buffer.resize(length);
    BIO_free_all(bio);

    return buffer;
}

namespace px
{
    // ===== AES-256-GCM 加密 =====
    std::string AuthAes::AesEncrypt(const std::string &plaintext, const unsigned char key[32]) {
        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

        unsigned char nonce[12];
        if (!RAND_bytes(nonce, sizeof(nonce))) {
            EVP_CIPHER_CTX_free(ctx);
            throw std::runtime_error("RAND_bytes failed");
        }

        if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
            throw std::runtime_error("EncryptInit failed");

        if (1 != EVP_EncryptInit_ex(ctx, NULL, NULL, key, nonce))
            throw std::runtime_error("EncryptInit key/nonce failed");

        std::vector<unsigned char> ciphertext(plaintext.size() + 16);
        int len;
        if (1 != EVP_EncryptUpdate(ctx, ciphertext.data(), &len,
                                   reinterpret_cast<const unsigned char *>(plaintext.data()),
                                   plaintext.size()))
            throw std::runtime_error("EncryptUpdate failed");
        int ciphertext_len = len;

        if (1 != EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len))
            throw std::runtime_error("EncryptFinal failed");
        ciphertext_len += len;

        unsigned char tag[16];
        if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag))
            throw std::runtime_error("Get TAG failed");

        EVP_CIPHER_CTX_free(ctx);

        // 拼接 nonce + ciphertext + tag
        std::vector<unsigned char> result;
        result.insert(result.end(), nonce, nonce + sizeof(nonce));
        result.insert(result.end(), ciphertext.begin(), ciphertext.begin() + ciphertext_len);
        result.insert(result.end(), tag, tag + sizeof(tag));

        return base64_encode(result);
    }

    // ===== AES-256-GCM 解密 =====
    std::string AuthAes::AesDecrypt(const std::string &encoded, const unsigned char key[32]) {
        std::vector<unsigned char> data = base64_decode(encoded);

        if (data.size() < 12 + 16) throw std::runtime_error("Invalid data");

        const unsigned char *nonce = data.data();
        const unsigned char *ciphertext = data.data() + 12;
        size_t ciphertext_len = data.size() - 12 - 16;
        const unsigned char *tag = data.data() + 12 + ciphertext_len;

        EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
        if (!ctx) throw std::runtime_error("EVP_CIPHER_CTX_new failed");

        if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL))
            throw std::runtime_error("DecryptInit failed");

        if (1 != EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce))
            throw std::runtime_error("DecryptInit key/nonce failed");

        std::vector<unsigned char> plaintext(ciphertext_len);
        int len;
        if (1 != EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext, ciphertext_len))
            throw std::runtime_error("DecryptUpdate failed");
        int plaintext_len = len;

        if (1 != EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, (void *) tag))
            throw std::runtime_error("Set TAG failed");

        int ret = EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len);
        EVP_CIPHER_CTX_free(ctx);

        if (ret <= 0) throw std::runtime_error("DecryptFinal failed (tag mismatch)");

        plaintext_len += len;
        return std::string(reinterpret_cast<char *>(plaintext.data()), plaintext_len);
    }

}