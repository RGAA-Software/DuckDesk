//
// Unit tests for ct_auth_token.cpp.
// Run with:
//   g++ -std=c++23 -I D:/source/GoCloud/GammaRayPremium/src \
//       ct_auth_token_test.cpp ct_auth_token.cpp \
//       -L /d/software/mingw64/opt/lib -lcrypto -o ct_auth_token_test \
//       && ./ct_auth_token_test
//

#include <cassert>
#include <iostream>
#include <cstring>
#include "ct_auth_token.h"

int main() {
    using namespace px;

    // Known answer test: verify the token matches a value produced by the Rust
    // px_auth_mgr::auth_token implementation for the same inputs.
    {
        const std::string appkey = "test-appkey-42";
        const int64_t ts = 1718755200000LL;
        const std::string nonce = "a1b2c3d4e5f60718";
        const std::string expected_app_secret = "cf80d5e86a912f08ff1c0c23723b3f20";
        const std::string expected_token =
            "baae2659ab82d91c75f618e81a02d070b089abbaaf2f8fe643e9fdcc7a0de49b";

        assert(CalculateAppSecret(appkey) == expected_app_secret);

        ConnectionToken token = GenerateConnectionToken(appkey, ts, nonce);
        assert(token.ts == ts);
        assert(token.nonce == nonce);
        assert(token.token == expected_token);
        std::cout << "KAT token: " << token.token << std::endl;
    }

    // Fresh tokens must be non-empty and different from each other.
    {
        ConnectionToken t1 = GenerateConnectionToken("appkey-1");
        ConnectionToken t2 = GenerateConnectionToken("appkey-1");
        assert(!t1.token.empty());
        assert(!t2.token.empty());
        assert(t1.token != t2.token);
        assert(t1.nonce != t2.nonce);
        assert(t1.token.length() == 64); // hex-encoded SHA256
    }

    // Different appkeys must produce different secrets and tokens.
    {
        const std::string secret_a = CalculateAppSecret("appkey-a");
        const std::string secret_b = CalculateAppSecret("appkey-b");
        assert(secret_a != secret_b);

        ConnectionToken t_a = GenerateConnectionToken("appkey-a", 12345, "nonce");
        ConnectionToken t_b = GenerateConnectionToken("appkey-b", 12345, "nonce");
        assert(t_a.token != t_b.token);
    }

    std::cout << "All ct_auth_token tests passed." << std::endl;
    return 0;
}
