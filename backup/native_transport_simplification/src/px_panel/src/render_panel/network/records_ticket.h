//
// Created by RGAA on 2026/08/17.
//
// Ticket signing / verification for the /records HTTP API (design doc
// docs/console_render_records_view_design.md section 5.3).
//
// console signs a short-lived ticket with HMAC-SHA256:
//   list/info : HMAC(device_id + "|" + "*"       + "|" + exp, key)
//   download  : HMAC(device_id + "|" + filename  + "|" + exp, key)
// key = MD5 hex string of the device security password (the value console stores
// as safety_pwd_md5). tk is the lowercase hex of the HMAC, exp is unix seconds.
//
// OpenSSL only, no Qt / asio2, so it can be unit tested standalone.
//

#ifndef TC_APPLICATION_RECORDS_TICKET_H
#define TC_APPLICATION_RECORDS_TICKET_H

#include <cstdint>
#include <string>

namespace px
{

    std::string RecordsMd5Hex(const std::string& input);

    std::string RecordsHmacSha256Hex(const std::string& key, const std::string& message);

    // MD5 hex of a RAW security password. Note the panel stores the safety
    // password already md5-hashed (PxSettings::GetDeviceSecurityPwd), so the
    // runtime HMAC key is that stored value used DIRECTLY — do not pass it
    // through this function again (records_http_handler::CheckTicket).
    std::string MakeRecordsTicketKey(const std::string& device_security_pwd);

    // filename is the concrete file name for downloads, "*" for list/info endpoints.
    std::string SignRecordsTicket(const std::string& device_id,
                                  const std::string& filename,
                                  int64_t exp_unix,
                                  const std::string& ticket_key);

    // Constant-time comparison of the provided ticket against the expected one.
    // Fails when tk is empty, exp is not positive, or exp < now_unix.
    bool VerifyRecordsTicket(const std::string& device_id,
                             const std::string& filename_or_star,
                             int64_t exp_unix,
                             const std::string& tk_hex,
                             const std::string& ticket_key,
                             int64_t now_unix);

}

#endif //TC_APPLICATION_RECORDS_TICKET_H
