#include <gtest/gtest.h>

#include <string>

#include "client_launch_config.h"
#include "px_common/console_frontend_relay_credential.h"

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
        "schema":1,"host":"127.0.0.1","local_host":"192.168.31.6","port":4601,"stream_id":"file-1","stream_name":"MC-60","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash","mode":"file-transfer",
        "only_viewing":true,"audio":false,"clipboard":false
    })");
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->fileTransferOnly);
    EXPECT_TRUE(config->viewOnly);
    EXPECT_FALSE(config->audio);
    EXPECT_FALSE(config->clipboard);
    EXPECT_EQ(config->streamName, "MC-60");
    EXPECT_EQ(config->localHost, "192.168.31.6");
}

TEST(ClientImguiLaunchConfigTest, AcceptanceFileTransferRequiresExplicitProcessMode) {
    constexpr std::string_view envelope{R"({
        "schema":1,"host":"127.0.0.1","port":4601,"stream_id":"file-acceptance","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "acceptance_file_transfer":{"local_source_path":"C:\\source\\payload.bin","remote_directory":"C:\\Windows\\Temp",
        "local_download_directory":"C:\\download","exercise_cancel_retry":true}
    })"};
    EXPECT_FALSE(ParseClientLaunchEnvelope(envelope));
    const auto config = ParseClientLaunchEnvelope(envelope, true);
    ASSERT_TRUE(config);
    ASSERT_TRUE(config->fileTransferAcceptance);
    EXPECT_EQ(config->fileTransferAcceptance->localSourcePath, "C:\\source\\payload.bin");
    EXPECT_EQ(config->fileTransferAcceptance->remoteDirectory, "C:\\Windows\\Temp");
    EXPECT_EQ(config->fileTransferAcceptance->localDownloadDirectory, "C:\\download");
    EXPECT_TRUE(config->fileTransferAcceptance->exerciseCancelRetry);
    EXPECT_FALSE(config->fileTransferAcceptance->exerciseHostRestart);
}

TEST(ClientImguiLaunchConfigTest, AcceptanceFileTransferHostRestartIsExplicitAndExclusive) {
    const auto config = ParseClientLaunchEnvelope(
        R"({"schema":1,"host":"127.0.0.1","port":4601,"stream_id":"file-restart","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "acceptance_file_transfer":{"local_source_path":"C:\\source\\payload.bin","remote_directory":"C:\\Windows\\Temp",
        "local_download_directory":"C:\\download","exercise_host_restart":true}})",
        true);
    ASSERT_TRUE(config);
    ASSERT_TRUE(config->fileTransferAcceptance);
    EXPECT_TRUE(config->fileTransferAcceptance->exerciseHostRestart);
    EXPECT_FALSE(config->fileTransferAcceptance->exerciseCancelRetry);
    EXPECT_FALSE(ParseClientLaunchEnvelope(
        R"({"schema":1,"host":"127.0.0.1","port":4601,"stream_id":"file-conflict","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "acceptance_file_transfer":{"local_source_path":"C:\\source\\payload.bin","remote_directory":"C:\\Windows\\Temp",
        "local_download_directory":"C:\\download","exercise_host_restart":true,"exercise_cancel_retry":true}})",
        true));
}

TEST(ClientImguiLaunchConfigTest, AcceptanceFileTransferRejectsMissingPaths) {
    EXPECT_FALSE(ParseClientLaunchEnvelope(
        R"({"schema":1,"host":"127.0.0.1","port":4601,"stream_id":"file-acceptance","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "acceptance_file_transfer":{"local_source_path":"C:\\source\\payload.bin","remote_directory":"C:\\Windows\\Temp"}})",
        true));
}

TEST(ClientImguiLaunchConfigTest, AcceptanceAudioRequiresExplicitProcessMode) {
    constexpr std::string_view envelope{R"({
        "schema":1,"host":"127.0.0.1","port":4601,"stream_id":"audio-acceptance","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash","acceptance_audio":true
    })"};
    EXPECT_FALSE(ParseClientLaunchEnvelope(envelope));
    const auto config = ParseClientLaunchEnvelope(envelope, true);
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->audioAcceptance);
}

TEST(ClientImguiLaunchConfigTest, AcceptanceModesAreMutuallyExclusive) {
    EXPECT_FALSE(ParseClientLaunchEnvelope(
        R"({"schema":1,"host":"127.0.0.1","port":4601,"stream_id":"acceptance","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash","acceptance_audio":true,
        "acceptance_file_transfer":{"local_source_path":"C:\\source\\payload.bin","remote_directory":"C:\\Windows\\Temp",
        "local_download_directory":"C:\\download"}})",
        true));
}

TEST(ClientImguiLaunchConfigTest, AcceptanceRdpIoErrorRequiresExplicitRdpProcessMode) {
    constexpr std::string_view envelope{R"({
        "schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","connection_instance_id":"instance-1",
        "frontend_session_id":"rdp-1","frontend_session_revision":2,"frontend_token":"frontend-secret",
        "acceptance_rdp_io_error":true,
        "rdp":{"schema":1,"account_name":"pxrdp_0123456789abcd","domain":"PIXELS",
        "password":"a-secure-workspace-password-with-32-bytes",
        "proxy_certificate_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}
    })"};
    EXPECT_FALSE(ParseClientLaunchEnvelope(envelope));
    const auto config = ParseClientLaunchEnvelope(envelope, true);
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->rdp);
    EXPECT_TRUE(config->rdpIoErrorAcceptance);
}

TEST(ClientImguiLaunchConfigTest, AcceptanceRdpIoErrorRejectsNonRdpAndOtherAcceptanceModes) {
    EXPECT_FALSE(ParseClientLaunchEnvelope(
        R"({"schema":1,"host":"127.0.0.1","port":4601,"stream_id":"native","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "acceptance_rdp_io_error":true})",
        true));
    EXPECT_FALSE(ParseClientLaunchEnvelope(
        R"({"schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","connection_instance_id":"instance-1",
        "frontend_session_id":"rdp-1","frontend_session_revision":2,"frontend_token":"frontend-secret",
        "acceptance_rdp_io_error":true,"acceptance_audio":true,
        "rdp":{"schema":1,"account_name":"pxrdp_0123456789abcd","domain":"PIXELS",
        "password":"a-secure-workspace-password-with-32-bytes",
        "proxy_certificate_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}})",
        true));
}

TEST(ClientImguiLaunchConfigTest, AcceptanceRdpPeerCloseRequiresExplicitRdpProcessMode) {
    constexpr std::string_view envelope{R"({
        "schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","connection_instance_id":"instance-1",
        "frontend_session_id":"rdp-1","frontend_session_revision":2,"frontend_token":"frontend-secret",
        "acceptance_rdp_peer_close":true,
        "rdp":{"schema":1,"account_name":"pxrdp_0123456789abcd","domain":"PIXELS",
        "password":"a-secure-workspace-password-with-32-bytes",
        "proxy_certificate_sha256":"0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"}
    })"};
    EXPECT_FALSE(ParseClientLaunchEnvelope(envelope));
    const auto config = ParseClientLaunchEnvelope(envelope, true);
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->rdpPeerCloseAcceptance);
}

TEST(ClientImguiLaunchConfigTest, RejectsLegacyCommandLineAndMissingPassword) {
    EXPECT_FALSE(ParseClientLaunchEnvelope("--host=127.0.0.1 --port=4601"));
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({"schema":1,"host":"127.0.0.1","port":4601})"));
}

TEST(ClientImguiLaunchConfigTest, ParsesConsoleFrontendDescriptorWithoutDevicePassword) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"render.example.test","port":4613,
        "stream_id":"930ef9cc-5817-4b88-90a2-17fc4ecfbb2a","device_id":"windows-client",
        "remote_device_id":"879557de-f121-4797-b824-00ddf9cf746e","connection_nonce":"nonce",
        "connection_instance_id":"879557de-f121-4797-b824-00ddf9cf746e",
        "frontend_session_id":"930ef9cc-5817-4b88-90a2-17fc4ecfbb2a","frontend_session_revision":2,
        "frontend_token":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    })");
    ASSERT_TRUE(config);
    EXPECT_EQ(config->frontendSessionId, "930ef9cc-5817-4b88-90a2-17fc4ecfbb2a");
    EXPECT_EQ(config->frontendSessionRevision, 2);
    ASSERT_TRUE(config->frontendToken);
    EXPECT_EQ(config->frontendToken->View(), "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");
    EXPECT_TRUE(config->remotePasswordHash.empty());
    const auto mediaPath = BuildClientMediaPath(*config);
    const auto fileTransferPath = BuildClientFileTransferPath(*config);
    EXPECT_NE(mediaPath.find("session_id=930ef9cc-5817-4b88-90a2-17fc4ecfbb2a"), std::string::npos);
    EXPECT_NE(mediaPath.find("session_revision=2"), std::string::npos);
    EXPECT_NE(mediaPath.find("frontend_token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), std::string::npos);
    EXPECT_EQ(mediaPath.find("safety_pwd_md5"), std::string::npos);
    EXPECT_NE(fileTransferPath.find("frontend_token=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"), std::string::npos);
}

TEST(ClientImguiLaunchConfigTest, RejectsMismatchedConsoleFrontendSession) {
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"render.example.test","port":4613,"stream_id":"stream-a","device_id":"windows-client",
        "remote_device_id":"instance","connection_nonce":"nonce","frontend_session_id":"stream-b","frontend_session_revision":2,
        "frontend_token":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
    })"));
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"render.example.test","port":4613,"stream_id":"stream-a","device_id":"windows-client",
        "remote_device_id":"instance","remote_password_hash":"password-must-not-enable-downgrade","connection_nonce":"nonce",
        "frontend_session_id":"stream-a","frontend_session_revision":2
    })"));
}

TEST(ClientImguiLaunchConfigTest, BuildsVersionedRelayFrontendCredential) {
    const auto encoded = BuildConsoleFrontendRelayCredential(7, "frontend-secret");
    const auto decoded = ParseConsoleFrontendRelayCredential(encoded);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->revision, 7);
    EXPECT_EQ(decoded->token, "frontend-secret");
    EXPECT_FALSE(ParseConsoleFrontendRelayCredential("frontend-secret"));
    EXPECT_FALSE(ParseConsoleFrontendRelayCredential("px-console-frontend-v1|0|frontend-secret"));
}

TEST(ClientImguiLaunchConfigTest, ParsesProtectedRdpLaunch) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","connection_instance_id":"instance-1",
        "frontend_session_id":"rdp-1","frontend_session_revision":2,"frontend_token":"frontend-secret",
        "rdp":{"schema":1,"account_name":"pxrdp_0123456789abcd","domain":"PIXELS",
        "proxy_certificate_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
               "password":"a-secure-workspace-password-with-32-bytes"}
    })");
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->rdp);
    ASSERT_TRUE(config->frontendToken);
    EXPECT_EQ(config->frontendToken->View(), "frontend-secret");
    EXPECT_EQ(config->rdpAccount, "pxrdp_0123456789abcd");
    ASSERT_TRUE(config->rdpPassword);
    EXPECT_EQ(config->rdpPassword->View(), "a-secure-workspace-password-with-32-bytes");
}

TEST(ClientImguiLaunchConfigTest, RejectsMalformedRdpIdentityAndPasswordType) {
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","frontend_session_id":"rdp-1",
        "frontend_session_revision":2,"frontend_token":"frontend-secret",
        "rdp":{"schema":1,"account_name":"Administrator","domain":"PIXELS",
        "proxy_certificate_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","password":"secret"}
    })"));
    EXPECT_FALSE(ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":5403,"stream_id":"rdp-1","device_id":"client-1",
        "remote_device_id":"render-1","connection_nonce":"nonce","frontend_session_id":"rdp-1",
        "frontend_session_revision":2,"frontend_token":"frontend-secret",
        "rdp":{"schema":1,"account_name":"pxrdp_0123456789abcd","domain":"PIXELS",
        "proxy_certificate_sha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","password":42}
    })"));
}

}  // namespace px::client::imgui
