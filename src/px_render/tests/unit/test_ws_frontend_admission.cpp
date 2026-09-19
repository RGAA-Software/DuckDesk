#include <gtest/gtest.h>

#include <string>
#include <unordered_map>

#include "px_render/network/ws/frontend_lease_renewal.h"

namespace px {
namespace {

std::unordered_map<std::string, std::string> ValidDescriptorQuery() {
    return {
        {"session_id", "session-42"},
        {"session_revision", "7"},
        {"stream_id", "session-42"},
        {"frontend_token", "frontend-secret"},
    };
}

ConsoleFrontendGrant ValidGrant() {
    return ConsoleFrontendGrant{
        .session_id = "session-42",
        .revision = 7,
        .target_kind = "cloud_application",
        .device_id = "render-node",
        .application_id = "application-8",
        .instance_id = "instance-9",
        .client_type = "windows",
        .access_role = "controller",
        .valid_for_ms = 30'000,
    };
}

TEST(WebSocketFrontendAdmissionTest, ConsumesCompleteDescriptorAndRemovesTokenFromQuery) {
    auto query_parameters = ValidDescriptorQuery();

    const auto descriptor = ConsumeWebSocketFrontendDescriptor(query_parameters);

    ASSERT_TRUE(descriptor);
    EXPECT_EQ(descriptor->session_id, "session-42");
    EXPECT_EQ(descriptor->revision, 7);
    EXPECT_EQ(descriptor->stream_id, "session-42");
    ASSERT_TRUE(descriptor->token);
    EXPECT_EQ(descriptor->token->Copy(), "frontend-secret");
    EXPECT_FALSE(query_parameters.contains("frontend_token"));
}

TEST(WebSocketFrontendAdmissionTest, RejectsIncompleteOrAmbiguousDescriptor) {
    auto missing_token = ValidDescriptorQuery();
    missing_token.erase("frontend_token");
    EXPECT_FALSE(ConsumeWebSocketFrontendDescriptor(missing_token));

    auto mismatched_stream = ValidDescriptorQuery();
    mismatched_stream["stream_id"] = "different-session";
    EXPECT_FALSE(ConsumeWebSocketFrontendDescriptor(mismatched_stream));

    auto revision_with_suffix = ValidDescriptorQuery();
    revision_with_suffix["session_revision"] = "7invalid";
    EXPECT_FALSE(ConsumeWebSocketFrontendDescriptor(revision_with_suffix));

    auto zero_revision = ValidDescriptorQuery();
    zero_revision["session_revision"] = "0";
    EXPECT_FALSE(ConsumeWebSocketFrontendDescriptor(zero_revision));
}

TEST(WebSocketFrontendAdmissionTest, AcceptsOnlyBoundCloudApplicationGrant) {
    auto query_parameters = ValidDescriptorQuery();
    const auto descriptor = ConsumeWebSocketFrontendDescriptor(query_parameters);
    ASSERT_TRUE(descriptor);
    const auto valid_grant = ValidGrant();

    EXPECT_TRUE(IsAcceptedWebSocketFrontendGrant(*descriptor, "instance-9", valid_grant));

    auto wrong_instance = valid_grant;
    wrong_instance.instance_id = "another-instance";
    EXPECT_FALSE(IsAcceptedWebSocketFrontendGrant(*descriptor, "instance-9", wrong_instance));

    auto wrong_target = valid_grant;
    wrong_target.target_kind = "device";
    EXPECT_FALSE(IsAcceptedWebSocketFrontendGrant(*descriptor, "instance-9", wrong_target));

    auto unsupported_role = valid_grant;
    unsupported_role.access_role = "administrator";
    EXPECT_FALSE(IsAcceptedWebSocketFrontendGrant(*descriptor, "instance-9", unsupported_role));

    auto expired_grant = valid_grant;
    expired_grant.valid_for_ms = 0;
    EXPECT_FALSE(IsAcceptedWebSocketFrontendGrant(*descriptor, "instance-9", expired_grant));
}

TEST(WebSocketFrontendAdmissionTest, RenewalCannotChangeFrontendIdentity) {
    const auto expected_grant = ValidGrant();
    EXPECT_TRUE(HasSameConsoleFrontendIdentity(expected_grant, expected_grant));

    auto changed_client_type = expected_grant;
    changed_client_type.client_type = "android";
    EXPECT_FALSE(HasSameConsoleFrontendIdentity(expected_grant, changed_client_type));

    auto changed_revision = expected_grant;
    ++changed_revision.revision;
    EXPECT_FALSE(HasSameConsoleFrontendIdentity(expected_grant, changed_revision));

    auto changed_role = expected_grant;
    changed_role.access_role = "observer";
    EXPECT_FALSE(HasSameConsoleFrontendIdentity(expected_grant, changed_role));
}

}  // namespace
}  // namespace px
