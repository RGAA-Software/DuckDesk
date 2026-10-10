#include <gtest/gtest.h>

#include <string>

#include "client_launch_config.h"
#include "px_common/console_frontend_relay_credential.h"

namespace px::client::imgui {

TEST(ClientImguiLaunchConfigTest, IrohLaunchKeepsExistingFrontendAuthorizationAndRelayPreference) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"private-node","port":4601,"stream_id":"session-1","device_id":"client",
        "remote_device_id":"remote","connection_nonce":"nonce","frontend_session_id":"session-1",
        "frontend_session_revision":2,"frontend_token":"existing-session-secret","force_relay":true,
        "iroh":{"endpoint_address":{"id":"node-identity","addrs":[{"Relay":"https://relay.example"}]},
        "endpoint_configuration":{"relays":[{"url":"https://relay.example","qad_port":4618}]}}
    })");
    ASSERT_TRUE(config);
    const auto parameters = BuildClientIrohParameters(*config);
    ASSERT_TRUE(parameters);
    EXPECT_EQ(parameters->frontend.at("frontend_token"), "existing-session-secret");
    EXPECT_EQ(parameters->frontend.at("session_revision"), "2");
    EXPECT_EQ(parameters->frontend.at("client_nonce"), "nonce");
    EXPECT_EQ(parameters->frontend.at("rdp"), "0");
    EXPECT_FALSE(parameters->frontend.contains("safety_pwd_md5"));
    const auto endpointConfiguration = nlohmann::json::parse(parameters->endpoint_configuration);
    EXPECT_TRUE(endpointConfiguration.at("relay_only").get<bool>());
    EXPECT_EQ(endpointConfiguration.at("relays").at(0).at("qad_port"), 4618);
}

TEST(ClientImguiLaunchConfigTest, IrohDirectAndRdpUseTheSameDescriptionWithoutLegacyRouteFallback) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"private-node","port":4601,"stream_id":"session-1","device_id":"client",
        "remote_device_id":"remote","connection_nonce":"nonce","remote_password_hash":"existing-password-digest",
        "iroh":{"endpoint_address":{"id":"node-identity","addrs":[{"Ip":"192.168.31.90:4601"}]},"endpoint_configuration":{}}
    })");
    ASSERT_TRUE(config);
    auto rdpConfig = *config;
    rdpConfig.rdp = true;
    const auto parameters = BuildClientIrohParameters(rdpConfig);
    ASSERT_TRUE(parameters);
    EXPECT_EQ(parameters->frontend.at("rdp"), "1");
    EXPECT_EQ(parameters->frontend.at("safety_pwd_md5"), "existing-password-digest");
    EXPECT_FALSE(parameters->frontend.contains("frontend_token"));
    EXPECT_EQ(parameters->endpoint_configuration, "{}");
    EXPECT_EQ(nlohmann::json::parse(parameters->endpoint_address).at("addrs").at(0).at("Ip"), "192.168.31.90:4601");
}

TEST(ClientImguiLaunchConfigTest, InvalidIrohDescriptionRejectsLaunchInsteadOfSelectingOldTransport) {
    constexpr std::string_view prefix{R"({"schema":1,"host":"private-node","port":4601,"stream_id":"session-1","device_id":"client",
        "remote_device_id":"remote","connection_nonce":"nonce","remote_password_hash":"hash","iroh":)"};
    for (const std::string_view invalid : {R"({})", R"({"endpoint_address":{},"endpoint_configuration":{}})",
                                          R"({"endpoint_address":{"id":"identity","addrs":[]},"endpoint_configuration":{}})",
                                          R"({"endpoint_address":{"id":"identity","addrs":[{"Ip":"127.0.0.1:4601"}]}})"}) {
        EXPECT_FALSE(ParseClientLaunchEnvelope(std::string{prefix} + std::string{invalid} + "}"));
    }
}

TEST(ClientImguiLaunchConfigTest, PanelStartupRequiresPortAndCanonicalLaunchIdentityTogether) {
    const std::string prefix = R"({"schema":1,"host":"127.0.0.1","port":4601,"stream_id":"stream",
        "device_id":"client","remote_device_id":"remote","connection_nonce":"nonce","remote_password_hash":"hash",)";
    const auto config = ParseClientLaunchEnvelope(prefix + R"("panel_port":4999,"panel_launch_id":"ee71cfc9-4ba0-443d-814c-b914cd86b9fe"})");
    ASSERT_TRUE(config);
    EXPECT_EQ(config->panelPort, 4999);
    EXPECT_EQ(config->panelLaunchId, "ee71cfc9-4ba0-443d-814c-b914cd86b9fe");
    for (const std::string suffix : {R"("panel_port":4999})", R"("panel_launch_id":"ee71cfc9-4ba0-443d-814c-b914cd86b9fe"})",
                                     R"("panel_port":65536,"panel_launch_id":"ee71cfc9-4ba0-443d-814c-b914cd86b9fe"})",
                                     R"("panel_port":-1,"panel_launch_id":"ee71cfc9-4ba0-443d-814c-b914cd86b9fe"})",
                                     R"("panel_port":4999,"panel_launch_id":"invalid&launch_id=another"})"}) {
        EXPECT_FALSE(ParseClientLaunchEnvelope(prefix + suffix));
    }
}

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
    EXPECT_FALSE(config->forceTcp);
    EXPECT_FALSE(config->forceRelay);
}

TEST(ClientImguiLaunchConfigTest, ExplicitTcpUsesDirectEndpointWithoutRelayConfiguration) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"192.168.31.90","port":4601,"stream_id":"direct-tcp","device_id":"client-uuid",
        "remote_device_id":"device-uuid","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "force_tcp":true,"force_relay":false
    })");
    ASSERT_TRUE(config);
    EXPECT_TRUE(config->forceTcp);
    EXPECT_FALSE(config->forceRelay);
    EXPECT_TRUE(config->relayHost.empty());
    EXPECT_EQ(config->relayPort, 0);
    EXPECT_EQ(config->host, "192.168.31.90");
    EXPECT_EQ(config->port, 4601);
}

TEST(ClientImguiLaunchConfigTest, ExplicitTcpOverridesAdvertisedIrohButKeepsExplicitRelayPriority) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"192.168.31.90","port":4601,"stream_id":"direct-tcp","device_id":"client",
        "remote_device_id":"remote","connection_nonce":"nonce","remote_password_hash":"hash","force_tcp":true,
        "iroh":{"endpoint_address":{"id":"identity","addrs":[{"Ip":"192.168.31.90:4601"}]},"endpoint_configuration":{}}
    })");
    ASSERT_TRUE(config);
    EXPECT_FALSE(BuildClientIrohParameters(*config));
    auto relay_config = *config;
    relay_config.forceRelay = true;
    const auto relay_parameters = BuildClientIrohParameters(relay_config);
    ASSERT_TRUE(relay_parameters);
    EXPECT_TRUE(nlohmann::json::parse(relay_parameters->endpoint_configuration).at("relay_only").get<bool>());
}

TEST(ClientImguiLaunchConfigTest, ParsesAppearanceWithoutChangingConnectionRequirements) {
    const auto config = ParseClientLaunchEnvelope(R"({
        "schema":1,"host":"127.0.0.1","port":4601,"stream_id":"direct-2","device_id":"100",
        "remote_device_id":"200","connection_nonce":"nonce","remote_password_hash":"password-hash",
        "language":"en-US","theme":"light","console_origin":"https://console.example:4600"
    })");
    ASSERT_TRUE(config);
    EXPECT_EQ(config->language, "en-US");
    EXPECT_TRUE(config->lightTheme);
    EXPECT_EQ(config->consoleOrigin, "https://console.example:4600");
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
