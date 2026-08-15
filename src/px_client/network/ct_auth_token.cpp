//
// Created by RGAA on 2025.
//

#include "ct_auth_token.h"
#include "px_common_new/time_util.h"

#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/md5.h>

#include <random>
#include <sstream>
#include <iomanip>
#include <cstring>

namespace px
{

    static std::string BytesToHex(const unsigned char* data, size_t len) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < len; ++i) {
            ss << std::setw(2) << static_cast<int>(data[i]);
        }
        return ss.str();
    }

    std::string CalculateAppSecret(const std::string& appkey) {
        const std::string salt = "bfa900206bed4db59156ae5fead1d249";
        const std::string input = appkey + salt;

        // Step 1: SHA256(appkey + salt)
        unsigned char sha_hash[SHA256_DIGEST_LENGTH];
        SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), sha_hash);
        std::string sha_hex = BytesToHex(sha_hash, SHA256_DIGEST_LENGTH);

        // Step 2: MD5(hex(sha_hash))
        unsigned char md5_hash[MD5_DIGEST_LENGTH];
        MD5(reinterpret_cast<const unsigned char*>(sha_hex.data()), sha_hex.size(), md5_hash);
        return BytesToHex(md5_hash, MD5_DIGEST_LENGTH);
    }

    static std::string GenerateNonce() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        std::uniform_int_distribution<uint64_t> dist;
        uint64_t a = dist(gen);
        uint64_t b = dist(gen);
        unsigned char bytes[16];
        std::memcpy(bytes, &a, sizeof(a));
        std::memcpy(bytes + sizeof(a), &b, sizeof(b));
        return BytesToHex(bytes, sizeof(bytes));
    }

    static std::string HmacSha256Hex(const std::string& key, const std::string& message) {
        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        HMAC(EVP_sha256(),
             key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(message.data()), message.size(),
             result, &len);
        return BytesToHex(result, len);
    }

    ConnectionToken GenerateConnectionToken(const std::string& appkey) {
        const int64_t ts = TimeUtil::GetCurrentTimestamp();
        const std::string nonce = GenerateNonce();
        return GenerateConnectionToken(appkey, ts, nonce);
    }

    ConnectionToken GenerateConnectionToken(const std::string& appkey,
                                            int64_t ts_ms,
                                            const std::string& nonce) {
        const std::string app_secret = CalculateAppSecret(appkey);
        const std::string message = std::format("{}|{}|{}", appkey, ts_ms, nonce);
        return ConnectionToken{
            .token = HmacSha256Hex(app_secret, message),
            .ts = ts_ms,
            .nonce = nonce,
        };
    }

} // namespace px
