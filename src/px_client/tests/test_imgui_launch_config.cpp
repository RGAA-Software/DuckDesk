#include "client_launch_config.h"

#include <gtest/gtest.h>

#include <string>

namespace px::client::imgui {

TEST(ClientImguiLaunchConfigTest, ParsesDirectPasswordLaunchWithoutTicket) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema": 1,
        "host": "127.0.0.1",
        "port": 4601,
        "stream_id": "direct-1",
        "device_id": "100",
        "remote_device_id": "200",
        "connection_nonce": "nonce",
        "remote_password_hash": "password-hash"
    })");
    ASSERT_TRUE(config);
    EXPECT_EQ(config->host, "127.0.0.1");
    EXPECT_EQ(config->port, 4601);
    EXPECT_EQ(config->remotePasswordHash, "password-hash");
}

TEST(ClientImguiLaunchConfigTest, RejectsLegacyCommandLineAndMissingPassword) {
    EXPECT_FALSE(ParseClientLaunchEnvelope("--host=127.0.0.1 --port=4601"));
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({"schema":1,"host":"127.0.0.1","port":4601})"));
}

TEST(ClientImguiLaunchConfigTest, ParsesProtectedRdpLaunchWithoutConsoleTicket) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","connection_instance_id":"instance-1",
        "remote_password_hash":"render-password-hash",
        "rdp":{"schema":1,"account_name":"grdp_user1","domain":"PIXELS",
        "proxy_certificate_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
               "password":"a-secure-workspace-password-with-32-bytes"}
    })");
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->rdp);
    EXPECT_EQ(config->rdpAccount, "grdp_user1");
    ASSERT_TRUE(config->rdpPassword);
    EXPECT_EQ(config->rdpPassword->View(), "a-secure-workspace-password-with-32-bytes");
}

} // namespace px::client::imgui
