//
// Created by RGAA on 2025.
//

#ifndef GAMMARAYPREMIUM_CT_AUTH_TOKEN_H
#define GAMMARAYPREMIUM_CT_AUTH_TOKEN_H

#include <string>
#include <cstdint>

namespace tc
{
    struct ConnectionToken {
        std::string token;
        int64_t ts = 0;
        std::string nonce;
    };

    /// Derives app_secret from appkey using the same algorithm as the Rust backend:
    /// md5(hex(sha256(appkey + salt)))
    std::string CalculateAppSecret(const std::string& appkey);

    /// Generates a fresh connection token for /spvr/client.
    ConnectionToken GenerateConnectionToken(const std::string& appkey);

    /// Generates a connection token from explicit timestamp and nonce. Useful for
    /// tests and for clients that want full control over the challenge material.
    ConnectionToken GenerateConnectionToken(const std::string& appkey,
                                            int64_t ts_ms,
                                            const std::string& nonce);

} // namespace tc

#endif //GAMMARAYPREMIUM_CT_AUTH_TOKEN_H
