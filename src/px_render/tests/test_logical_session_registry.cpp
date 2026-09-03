#include <gtest/gtest.h>

#include <algorithm>

#include "session/logical_session_registry.h"

namespace px {
namespace {

LogicalSessionGrant ControlGrant(const std::string& id, const std::string& stream,
                                 const std::string& subject) {
    return {.logical_session_id = id,
            .stream_id = stream,
            .subject_id = subject,
            .join_mode = "control",
            .expires_at_ms = 60'000};
}

TEST(LogicalSessionRegistry, ControllerAndObserverAreSeparatedByLease) {
    LogicalSessionRegistry registry;
    const auto controller = registry.Bind(
        ControlGrant("controller", "stream-controller", "alice"),
        LogicalSessionTransport::kRtcLocal, "rtc-controller", false, 1);
    ASSERT_EQ(controller.code, LogicalSessionAdmissionCode::kAccepted);
    EXPECT_TRUE(registry.AuthorizeControllerInputBinding("rtc-controller", 1));
    EXPECT_TRUE(registry.AuthorizeControllerInputStream("stream-controller", 1));
    ASSERT_EQ(controller.role, LogicalSessionRole::kController);
    EXPECT_TRUE(registry.AuthorizeControllerInput("controller", controller.lease_generation, 2));

    auto observer = ControlGrant("observer", "stream-observer", "bob");
    observer.join_mode = "observe";
    const auto admitted = registry.Bind(observer, LogicalSessionTransport::kRtcLocal,
                                        "rtc-observer", false, 2);
    ASSERT_EQ(admitted.code, LogicalSessionAdmissionCode::kAccepted);
    EXPECT_EQ(admitted.role, LogicalSessionRole::kObserver);
    EXPECT_FALSE(registry.AuthorizeControllerInput("observer", admitted.lease_generation, 2));
    EXPECT_FALSE(registry.AuthorizeControllerInputBinding("rtc-observer", 2));
    EXPECT_FALSE(registry.AuthorizeControllerInputStream("stream-observer", 2));
    EXPECT_EQ(registry.ActiveSessionCount(), 2U);
    EXPECT_EQ(registry.ObserverCount(), 1U);
}

TEST(LogicalSessionRegistry, TakeoverInvalidatesOldControllerLease) {
    LogicalSessionRegistry registry;
    const auto original = registry.Bind(ControlGrant("one", "stream-one", "alice"),
                                        LogicalSessionTransport::kRtcLocal, "rtc-one", false, 1);
    ASSERT_EQ(original.code, LogicalSessionAdmissionCode::kAccepted);
    const auto occupied = registry.Bind(ControlGrant("two", "stream-two", "bob"),
                                        LogicalSessionTransport::kRtcLocal, "rtc-two", false, 2);
    EXPECT_EQ(occupied.code, LogicalSessionAdmissionCode::kOccupied);

    const auto replacement = registry.Bind(ControlGrant("two", "stream-two", "bob"),
                                           LogicalSessionTransport::kRtcLocal, "rtc-two", true, 3);
    ASSERT_EQ(replacement.code, LogicalSessionAdmissionCode::kAccepted);
    EXPECT_TRUE(replacement.release_previous_controller_input);
    EXPECT_EQ(replacement.previous_controller_session_id, "one");
    EXPECT_EQ(replacement.previous_controller_lease_generation, original.lease_generation);
    EXPECT_FALSE(registry.AuthorizeControllerInput("one", original.lease_generation, 3));
    EXPECT_TRUE(registry.AuthorizeControllerInput("two", replacement.lease_generation, 3));
    EXPECT_EQ(registry.FindRole("one"), LogicalSessionRole::kObserver);
    EXPECT_EQ(registry.FindStreamId("one"), std::optional<std::string>{"stream-one"});
    const auto sessions = registry.SnapshotActive(4);
    const auto replacement_snapshot = std::find_if(sessions.begin(), sessions.end(),
        [](const LogicalSessionSnapshot& snapshot) { return snapshot.logical_session_id == "two"; });
    ASSERT_NE(replacement_snapshot, sessions.end());
    EXPECT_EQ(replacement_snapshot->takeover_previous_session_id, "one");
}

TEST(LogicalSessionRegistry, OldControllerDisconnectCannotReleaseReplacementLease) {
    LogicalSessionRegistry registry;
    const auto original = registry.Bind(ControlGrant("one", "stream-one", "alice"),
                                        LogicalSessionTransport::kRtcLocal, "rtc-one", false, 1);
    ASSERT_EQ(original.code, LogicalSessionAdmissionCode::kAccepted);
    const auto replacement = registry.Bind(ControlGrant("two", "stream-two", "bob"),
                                           LogicalSessionTransport::kRtcLocal, "rtc-two", true, 2);
    ASSERT_EQ(replacement.code, LogicalSessionAdmissionCode::kAccepted);

    const auto old_closed = registry.CloseBinding("one", "rtc-one", 3);
    EXPECT_FALSE(old_closed.release_controller_input);
    EXPECT_TRUE(registry.AuthorizeControllerInput("two", replacement.lease_generation, 4));
    EXPECT_TRUE(registry.AuthorizeControllerInputBinding("rtc-two", 4));
    EXPECT_FALSE(registry.AuthorizeControllerInputBinding("rtc-one", 4));
}

TEST(LogicalSessionRegistry, InputLeaseIdentifiesTheExactControllerGeneration) {
    LogicalSessionRegistry registry;
    const auto original = registry.Bind(ControlGrant("one", "stream-one", "alice"),
                                        LogicalSessionTransport::kWs, "ws-one", false, 1);
    ASSERT_EQ(original.code, LogicalSessionAdmissionCode::kAccepted);
    const auto lease = registry.FindControllerInputLeaseByBinding("ws-one", 2);
    ASSERT_TRUE(lease.has_value());
    EXPECT_EQ(lease->logical_session_id, "one");
    EXPECT_EQ(lease->generation, original.lease_generation);

    const auto replacement = registry.Bind(ControlGrant("two", "stream-two", "bob"),
                                           LogicalSessionTransport::kWs, "ws-two", true, 3);
    ASSERT_EQ(replacement.code, LogicalSessionAdmissionCode::kAccepted);
    EXPECT_FALSE(registry.FindControllerInputLeaseByBinding("ws-one", 4).has_value());
    const auto current = registry.FindControllerInputLeaseByStream("stream-two", 4);
    ASSERT_TRUE(current.has_value());
    EXPECT_EQ(current->generation, replacement.lease_generation);
}

TEST(LogicalSessionRegistry, ActiveSnapshotDeduplicatesTransportBindings) {
    LogicalSessionRegistry registry;
    ASSERT_EQ(registry.Bind(ControlGrant("one", "stream-one", "alice"),
                            LogicalSessionTransport::kWs, "ws-one", false, 1).code,
              LogicalSessionAdmissionCode::kAccepted);
    ASSERT_EQ(registry.Bind(ControlGrant("one", "stream-one", "alice"),
                            LogicalSessionTransport::kWs, "ws-two", false, 2).code,
              LogicalSessionAdmissionCode::kAccepted);
    ASSERT_EQ(registry.Bind(ControlGrant("one", "stream-one", "alice"),
                            LogicalSessionTransport::kFileTransfer, "file-one", false, 3).code,
              LogicalSessionAdmissionCode::kAccepted);
    const auto sessions = registry.SnapshotActive(4);
    ASSERT_EQ(sessions.size(), 1U);
    EXPECT_EQ(sessions.front().logical_session_id, "one");
    EXPECT_EQ(sessions.front().role, LogicalSessionRole::kController);
    EXPECT_EQ(sessions.front().transports.size(), 2U);
    EXPECT_EQ(std::count(sessions.front().transports.begin(), sessions.front().transports.end(),
                         LogicalSessionTransport::kWs), 1);
}

TEST(LogicalSessionRegistry, ControllerDisconnectReleasesInputAndCanReconnect) {
    LogicalSessionRegistry registry;
    const auto admitted = registry.Bind(ControlGrant("one", "stream-one", "alice"),
                                        LogicalSessionTransport::kWs, "ws-one", false, 1);
    ASSERT_EQ(admitted.code, LogicalSessionAdmissionCode::kAccepted);
    const auto closed = registry.CloseBinding("one", "ws-one", 10);
    EXPECT_TRUE(closed.release_controller_input);
    EXPECT_FALSE(registry.AuthorizeControllerInput("one", admitted.lease_generation, 11));

    const auto reconnected = registry.Bind(ControlGrant("one", "stream-one", "alice"),
                                           LogicalSessionTransport::kWs, "ws-two", false, 12);
    ASSERT_EQ(reconnected.code, LogicalSessionAdmissionCode::kAccepted);
    EXPECT_GT(reconnected.lease_generation, admitted.lease_generation);
    EXPECT_TRUE(registry.AuthorizeControllerInput("one", reconnected.lease_generation, 13));
}

TEST(LogicalSessionRegistry, ControllerReconnectGraceKeepsOneLogicalSnapshot) {
    LogicalSessionRegistry registry;
    ASSERT_EQ(registry.Bind(ControlGrant("one", "stream-one", "alice"),
                            LogicalSessionTransport::kWs, "ws-one", false, 1).code,
              LogicalSessionAdmissionCode::kAccepted);
    ASSERT_TRUE(registry.CloseBinding("one", "ws-one", 10).release_controller_input);

    const auto reconnecting = registry.SnapshotActive(11);
    ASSERT_EQ(reconnecting.size(), 1U);
    EXPECT_EQ(reconnecting.front().logical_session_id, "one");
    EXPECT_TRUE(reconnecting.front().transports.empty());

    EXPECT_TRUE(registry.SnapshotActive(5'011).empty());
}

TEST(LogicalSessionRegistry, FileTransferBindingNeverExtendsControllerInputLease) {
    LogicalSessionRegistry registry;
    const auto controller = registry.Bind(ControlGrant("one", "stream-one", "alice"),
                                          LogicalSessionTransport::kWs, "ws-one", false, 1);
    ASSERT_EQ(controller.code, LogicalSessionAdmissionCode::kAccepted);
    ASSERT_EQ(registry.Bind(ControlGrant("one", "stream-one", "alice"),
                            LogicalSessionTransport::kFileTransfer, "file-one", false, 2).code,
              LogicalSessionAdmissionCode::kAccepted);

    EXPECT_FALSE(registry.CloseBinding("one", "file-one", 3).release_controller_input);
    EXPECT_TRUE(registry.AuthorizeControllerInput("one", controller.lease_generation, 4));
    EXPECT_TRUE(registry.CloseBindingById("ws-one", 5).release_controller_input);
    EXPECT_FALSE(registry.AuthorizeControllerInput("one", controller.lease_generation, 6));
}

TEST(LogicalSessionRegistry, FileTransferCanCreateControllerSessionWithoutInputBinding) {
    LogicalSessionRegistry registry;
    const auto admitted = registry.Bind(
        ControlGrant("one", "stream-one", "alice"),
        LogicalSessionTransport::kFileTransfer, "file-one", false, 1);
    ASSERT_EQ(admitted.code, LogicalSessionAdmissionCode::kAccepted);
    EXPECT_EQ(admitted.role, LogicalSessionRole::kController);
    EXPECT_FALSE(registry.AuthorizeControllerInputBinding("file-one", 2));
    EXPECT_TRUE(registry.FindControllerLeaseByBinding("file-one", 2).has_value());
    EXPECT_TRUE(registry.CloseBinding("one", "file-one", 3).release_controller_input);
}

TEST(LogicalSessionRegistry, FindsAnyActiveRoleByTransportBindingWithoutMutatingIt) {
    LogicalSessionRegistry registry;
    auto observer_grant = ControlGrant("observer", "stream-observer", "bob");
    observer_grant.join_mode = "observe";
    ASSERT_EQ(registry.Bind(observer_grant, LogicalSessionTransport::kRtcLocal,
                            "rtc-local:stream-observer", false, 1).code,
              LogicalSessionAdmissionCode::kAccepted);

    const auto logical_session_id = registry.FindLogicalSessionIdByBinding(
        "rtc-local:stream-observer", 2);
    ASSERT_TRUE(logical_session_id.has_value());
    EXPECT_EQ(*logical_session_id, "observer");
    EXPECT_EQ(registry.ActiveSessionCount(), 1U);
    EXPECT_FALSE(registry.FindLogicalSessionIdByBinding("missing", 2).has_value());
    EXPECT_TRUE(registry.FindLogicalSessionIdByBinding(
        "rtc-local:stream-observer", 60'001).has_value());
}

TEST(LogicalSessionRegistry, GrantExpiryOnlyLimitsAdmissionNotAnEstablishedBinding) {
    LogicalSessionRegistry registry;
    const auto grant = ControlGrant("one", "stream-one", "alice");
    const auto admitted = registry.Bind(
        grant, LogicalSessionTransport::kRtcLocal, "rtc-one", false, 59'999);
    ASSERT_EQ(admitted.code, LogicalSessionAdmissionCode::kAccepted);

    EXPECT_TRUE(registry.AuthorizeControllerInput(
        "one", admitted.lease_generation, 120'000));
    EXPECT_TRUE(registry.FindControllerInputLeaseByBinding(
        "rtc-one", 120'000).has_value());
    EXPECT_EQ(registry.SnapshotActive(120'000).size(), 1U);

    EXPECT_EQ(registry.Bind(ControlGrant("two", "stream-two", "bob"),
                            LogicalSessionTransport::kRtcLocal, "rtc-two", false, 60'000).code,
              LogicalSessionAdmissionCode::kExpired);
}

TEST(LogicalSessionRegistry, TicketPolicyRejectsObserverAndTakeover) {
    LogicalSessionRegistry registry;
    auto controller_grant = ControlGrant("one", "stream-one", "alice");
    controller_grant.allow_takeover = false;
    const auto controller = registry.Bind(
        controller_grant, LogicalSessionTransport::kRtcLocal, "rtc-one", false, 1);
    ASSERT_EQ(controller.code, LogicalSessionAdmissionCode::kAccepted);

    auto observer_grant = ControlGrant("observer", "stream-observer", "bob");
    observer_grant.join_mode = "observe";
    observer_grant.allow_observer = false;
    EXPECT_EQ(registry.Bind(observer_grant, LogicalSessionTransport::kRtcLocal,
                            "rtc-observer", false, 2).code,
              LogicalSessionAdmissionCode::kObserversDisabled);

    const auto replacement = registry.Bind(
        ControlGrant("two", "stream-two", "carol"), LogicalSessionTransport::kRtcLocal,
        "rtc-two", true, 3);
    EXPECT_EQ(replacement.code, LogicalSessionAdmissionCode::kTakeoverDisabled);
}

} // namespace
} // namespace px
