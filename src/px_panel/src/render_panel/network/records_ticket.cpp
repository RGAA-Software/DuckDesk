//
// Created by RGAA on 2026/08/17.
//

#include "records_ticket.h"

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>

#include <cctype>
#include <iomanip>
#include <sstream>

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

    std::string RecordsMd5Hex(const std::string& input) {
        unsigned char digest[MD5_DIGEST_LENGTH];
        MD5(reinterpret_cast<const unsigned char*>(input.data()), input.size(), digest);
        return BytesToHex(digest, MD5_DIGEST_LENGTH);
    }

    std::string RecordsHmacSha256Hex(const std::string& key, const std::string& message) {
        unsigned char result[EVP_MAX_MD_SIZE];
        unsigned int len = 0;
        HMAC(EVP_sha256(),
             key.data(), static_cast<int>(key.size()),
             reinterpret_cast<const unsigned char*>(message.data()), message.size(),
             result, &len);
        return BytesToHex(result, len);
    }

    std::string MakeRecordsTicketKey(const std::string& device_security_pwd) {
        return RecordsMd5Hex(device_security_pwd);
    }

    std::string SignRecordsTicket(const std::string& device_id,
                                  const std::string& filename,
                                  int64_t exp_unix,
                                  const std::string& ticket_key) {
        const std::string message = device_id + "|" + filename + "|" + std::to_string(exp_unix);
        return RecordsHmacSha256Hex(ticket_key, message);
    }

    // constant-time equality for same-length strings
    static bool SecureEquals(const std::string& a, const std::string& b) {
        if (a.size() != b.size()) {
            return false;
        }
        unsigned char diff = 0;
        for (size_t i = 0; i < a.size(); ++i) {
            diff |= static_cast<unsigned char>(a[i] ^ b[i]);
        }
        return diff == 0;
    }

    bool VerifyRecordsTicket(const std::string& device_id,
                             const std::string& filename_or_star,
                             int64_t exp_unix,
                             const std::string& tk_hex,
                             const std::string& ticket_key,
                             int64_t now_unix) {
        if (tk_hex.empty() || device_id.empty()) {
            return false;
        }
        if (exp_unix <= 0 || exp_unix < now_unix) {
            return false;
        }
        // normalize to lowercase so upper-case hex from a client still matches,
        // the comparison below stays constant-time over equal-length buffers
        std::string tk_lower = tk_hex;
        for (auto& c : tk_lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        const std::string expected = SignRecordsTicket(device_id, filename_or_star, exp_unix, ticket_key);
        return SecureEquals(expected, tk_lower);
    }

}
