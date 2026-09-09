#include <gtest/gtest.h>

#include <Windows.h>
#include <array>
#include <string>

#include "px_common/http_client.h"

namespace {

std::string TestCa(bool invalid = false) {
    std::array<char, 32768> path{};
    const auto size =
        GetEnvironmentVariableA(invalid ? "GAMMARAY_RDP_TEST_INVALID_CA" : "GAMMARAY_RDP_TEST_CA", path.data(), static_cast<DWORD>(path.size()));
    return size > 0 && size < path.size() ? std::string{path.data(), size} : std::string{};
}

void CheckMethods(const std::string& host, const std::string& ca, bool accepted) {
    // The local acceptance Console serves on 30500. This nonexistent resource
    // cannot create users, change application state or return credentials.
    const auto client = px::HttpClient::MakeSSL(host, 30500, "/__rdp_tls_probe__/not-found", 2000);
    client->SetTrustedCaFile(ca);
    ASSERT_TRUE(client->IsPeerVerificationEnabled());
    const std::array responses{client->Request(), client->Post(), client->Patch({}, "{}", "application/json"), client->PostMultiPart({}, {}, {}),
                               client->PutMultiPart({}, {}, {})};
    for (const auto& response : responses) {
        if (accepted) {
            EXPECT_GT(response.status, 0);
            EXPECT_EQ(response.error_code, 0);
        } else {
            EXPECT_EQ(response.status, 0);
            EXPECT_EQ(response.error_code, static_cast<int>(cpr::ErrorCode::PEER_FAILED_VERIFICATION));
        }
    }
}

TEST(HttpPrivateCa, PrivateRootWithoutCrlStillValidatesAllRequestMethods) {
    const auto ca = TestCa();
    if (ca.empty()) {
        GTEST_SKIP() << "Set GAMMARAY_RDP_TEST_CA and start the local acceptance Console";
    }
    CheckMethods("localhost", ca, true);
}

TEST(HttpPrivateCa, WrongRootDoesNotFallBackToSystemTrust) {
    const auto ca = TestCa(true);
    if (ca.empty()) {
        GTEST_SKIP() << "Set GAMMARAY_RDP_TEST_INVALID_CA to a different valid CA certificate";
    }
    CheckMethods("localhost", ca, false);
}

TEST(HttpPrivateCa, WrongHostnameIsRejectedEvenWithCorrectRoot) {
    const auto ca = TestCa();
    if (ca.empty()) {
        GTEST_SKIP() << "Set GAMMARAY_RDP_TEST_CA; the test certificate must not contain 127.0.0.2";
    }
    CheckMethods("127.0.0.2", ca, false);
}

} // namespace
