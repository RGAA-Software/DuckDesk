//
// Unit tests for records_ticket (design doc section 9.1)
//

#include <gtest/gtest.h>

#include "render_panel/network/records_ticket.h"

// RFC 4231 test case 1: HMAC-SHA256
TEST(RecordsTicket, HmacSha256KnownVector) {
    // key = 20 bytes of 0x0b, data = "Hi There"
    const std::string key(20, '\x0b');
    const std::string hex = px::RecordsHmacSha256Hex(key, "Hi There");
    EXPECT_EQ(hex, "b0344c61d8db38535ca8afceaf0bf12b"
                   "881dc200c9833da726e9376c2e32cff7");
}

// MD5 known vector
TEST(RecordsTicket, Md5KnownVector) {
    EXPECT_EQ(px::RecordsMd5Hex("abc"), "900150983cd24fb0d6963f7d28e17f72");
    EXPECT_EQ(px::RecordsMd5Hex(""), "d41d8cd98f00b204e9800998ecf8427e");
}

TEST(RecordsTicket, TicketKeyIsMd5OfPwd) {
    EXPECT_EQ(px::MakeRecordsTicketKey("123456"), px::RecordsMd5Hex("123456"));
}

TEST(RecordsTicket, SignFormatForFile) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const std::string tk = px::SignRecordsTicket("dev1", "rec_A_20260817_10.30.00.mp4", 1700000000, key);
    EXPECT_EQ(tk, px::RecordsHmacSha256Hex(key, "dev1|rec_A_20260817_10.30.00.mp4|1700000000"));
    EXPECT_EQ(tk.size(), 64u);
}

TEST(RecordsTicket, SignFormatForList) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const std::string tk = px::SignRecordsTicket("dev1", "*", 1700000000, key);
    EXPECT_EQ(tk, px::RecordsHmacSha256Hex(key, "dev1|*|1700000000"));
}

TEST(RecordsTicket, VerifyOk) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const int64_t now = 1700000000;
    const int64_t exp = now + 600;
    const std::string tk = px::SignRecordsTicket("dev1", "f.mp4", exp, key);
    EXPECT_TRUE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, tk, key, now));
}

TEST(RecordsTicket, VerifyOkForWildcard) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const int64_t now = 1700000000;
    const int64_t exp = now + 600;
    const std::string tk = px::SignRecordsTicket("dev1", "*", exp, key);
    EXPECT_TRUE(px::VerifyRecordsTicket("dev1", "*", exp, tk, key, now));
}

TEST(RecordsTicket, VerifyExpired) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const int64_t now = 1700000000;
    const int64_t exp = now - 1;
    const std::string tk = px::SignRecordsTicket("dev1", "f.mp4", exp, key);
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, tk, key, now));
}

TEST(RecordsTicket, VerifyWrongTicket) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const int64_t now = 1700000000;
    const int64_t exp = now + 600;
    const std::string tk = px::SignRecordsTicket("dev1", "f.mp4", exp, key);
    // other file name
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "g.mp4", exp, tk, key, now));
    // other device
    EXPECT_FALSE(px::VerifyRecordsTicket("dev2", "f.mp4", exp, tk, key, now));
    // other key
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, tk, px::MakeRecordsTicketKey("other"), now));
    // tampered hex (flip one char, keep length)
    std::string bad = tk;
    bad[0] = (bad[0] == 'a') ? 'b' : 'a';
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, bad, key, now));
}

TEST(RecordsTicket, VerifyBadParams) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const int64_t now = 1700000000;
    const int64_t exp = now + 600;
    const std::string tk = px::SignRecordsTicket("dev1", "f.mp4", exp, key);
    // empty tk
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, "", key, now));
    // exp = 0 (param missing)
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "f.mp4", 0, tk, key, now));
    // wrong length tk
    EXPECT_FALSE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, "abcd", key, now));
    // empty device id
    EXPECT_FALSE(px::VerifyRecordsTicket("", "f.mp4", exp, tk, key, now));
}

TEST(RecordsTicket, VerifyAcceptsUpperCaseHex) {
    const std::string key = px::MakeRecordsTicketKey("pwd");
    const int64_t now = 1700000000;
    const int64_t exp = now + 600;
    std::string tk = px::SignRecordsTicket("dev1", "f.mp4", exp, key);
    for (auto& c : tk) c = (char)std::toupper((unsigned char)c);
    EXPECT_TRUE(px::VerifyRecordsTicket("dev1", "f.mp4", exp, tk, key, now));
}
