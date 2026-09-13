#include "client_launch_config.h"

#include <gtest/gtest.h>

#include <string>

namespace px::client::imgui {

TEST(ClientImguiLaunchConfigTest, ParsesDirectPasswordLaunch) {
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
    EXPECT_FALSE(config->lightTheme);
    EXPECT_TRUE(config->enhancedVisualEffects);
}

TEST(ClientImguiLaunchConfigTest, ParsesAppearanceWithoutChangingConnectionRequirements) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":4601,"stream_id":"direct-2","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "language":"en-US","theme":"light","enhanced_visual_effects":false
    })");
    ASSERT_TRUE(config);
    EXPECT_EQ(config->language, "en-US");
    EXPECT_TRUE(config->lightTheme);
    EXPECT_FALSE(config->enhancedVisualEffects);
}

TEST(ClientImguiLaunchConfigTest, ParsesIndependentFileTransferLaunch) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":4601,"stream_id":"file-1","stream_name":"MC-60","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash","mode":"file-transfer",
        "only_viewing":true,"audio":false,"clipboard":false
    })");
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->fileTransferOnly);
    EXPECT_TRUE(config->viewOnly);
    EXPECT_FALSE(config->audio);
    EXPECT_FALSE(config->clipboard);
    EXPECT_EQ(config->streamName, "MC-60");
}

TEST(ClientImguiLaunchConfigTest, RejectsLegacyCommandLineAndMissingPassword) {
    EXPECT_FALSE(ParseClientLaunchEnvelope("--host=127.0.0.1 --port=4601"));
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({"schema":1,"host":"127.0.0.1","port":4601})"));
}

TEST(ClientImguiLaunchConfigTest, ParsesProtectedRdpLaunch) {
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
