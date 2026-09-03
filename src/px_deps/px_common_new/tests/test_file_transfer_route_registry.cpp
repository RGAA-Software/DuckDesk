#include <gtest/gtest.h>

#include "px_common_new/file_transfer_route_registry.h"

namespace px {
namespace {

TEST(FileTransferRouteRegistry, UsesOnlyLatestInboundTransport) {
    FileTransferRouteRegistry registry;
    const auto ws = registry.Bind("stream-a", "net_ws");
    ASSERT_EQ(registry.Resolve("stream-a"), ws);

    const auto rtc = registry.Bind("stream-a", "net_rtc_local");
    ASSERT_EQ(registry.Resolve("stream-a"), rtc);
    EXPECT_NE(ws.generation, rtc.generation);
    EXPECT_FALSE(registry.RemoveIfGenerationMatches("stream-a", ws.generation));
    EXPECT_EQ(registry.Resolve("stream-a"), rtc);

    EXPECT_TRUE(registry.RemoveIfGenerationMatches("stream-a", rtc.generation));
    EXPECT_FALSE(registry.Resolve("stream-a").has_value());
}

TEST(FileTransferRouteRegistry, RepeatedTrafficKeepsGenerationAndStaleTransportCannotRemoveRoute) {
    FileTransferRouteRegistry registry;
    const auto first = registry.Bind("stream-a", "net_ws");
    const auto repeated = registry.Bind("stream-a", "net_ws");
    EXPECT_EQ(first, repeated);

    const auto replacement = registry.Bind("stream-a", "net_rtc_local");
    EXPECT_NE(first.generation, replacement.generation);
    EXPECT_FALSE(registry.RemoveIfPluginMatches("stream-a", "net_ws"));
    EXPECT_EQ(registry.Resolve("stream-a"), replacement);
    EXPECT_TRUE(registry.RemoveIfPluginMatches("stream-a", "net_rtc_local"));
    EXPECT_FALSE(registry.Resolve("stream-a").has_value());
}

TEST(FileTransferRouteRegistry, SamePluginReconnectUsesConnectionGeneration) {
    FileTransferRouteRegistry registry;
    const auto old_connection = registry.Bind(
        "stream-a", "net_rtc_local", "rtc-connection-1");
    const auto repeated = registry.Bind(
        "stream-a", "net_rtc_local", "rtc-connection-1");
    EXPECT_EQ(old_connection, repeated);

    const auto new_connection = registry.Bind(
        "stream-a", "net_rtc_local", "rtc-connection-2");
    EXPECT_NE(old_connection.generation, new_connection.generation);
    EXPECT_EQ(registry.Resolve("stream-a"), new_connection);

    // A late close from the previous RTC instance must not tear down the
    // replacement route even though both instances use the same plug-in.
    EXPECT_FALSE(registry.RemoveIfConnectionMatches(
        "stream-a", "net_rtc_local", "rtc-connection-1"));
    EXPECT_EQ(registry.Resolve("stream-a"), new_connection);
    EXPECT_TRUE(registry.RemoveIfConnectionMatches(
        "stream-a", "net_rtc_local", "rtc-connection-2"));
    EXPECT_FALSE(registry.Resolve("stream-a").has_value());
}

TEST(FileTransferRouteRegistry, StreamsRemainIndependent) {
    FileTransferRouteRegistry registry;
    static_cast<void>(registry.Bind("stream-a", "net_ws"));
    static_cast<void>(registry.Bind("stream-b", "net_relay"));

    ASSERT_EQ(registry.Size(), 2U);
    ASSERT_EQ(registry.Resolve("stream-a")->plugin_id, "net_ws");
    ASSERT_EQ(registry.Resolve("stream-b")->plugin_id, "net_relay");

    EXPECT_TRUE(registry.Remove("stream-a"));
    EXPECT_FALSE(registry.Resolve("stream-a").has_value());
    EXPECT_TRUE(registry.Resolve("stream-b").has_value());
}

TEST(FileTransferRouteRegistry, LogicalSessionsWithSameStreamRemainIsolated) {
    FileTransferRouteRegistry registry;
    const auto first = registry.Bind("session-one", "stream-shared", "net_ws", "ws-one");
    const auto second = registry.Bind("session-two", "stream-shared", "net_ws", "ws-two");

    EXPECT_NE(first.generation, second.generation);
    ASSERT_EQ(registry.Resolve("session-one", "stream-shared"), first);
    ASSERT_EQ(registry.Resolve("session-two", "stream-shared"), second);
    EXPECT_FALSE(registry.ResolveUniqueStream("stream-shared").has_value());

    EXPECT_TRUE(registry.RemoveIfConnectionMatches(
        "session-one", "stream-shared", "net_ws", "ws-one"));
    EXPECT_FALSE(registry.Resolve("session-one", "stream-shared").has_value());
    ASSERT_EQ(registry.ResolveUniqueStream("stream-shared"), second);
}

TEST(FileTransferRouteRegistry, PluginOnlyDisconnectStaysWithinLogicalSession) {
    FileTransferRouteRegistry registry;
    static_cast<void>(registry.Bind("session-one", "stream-shared", "net_ws", "ws-one"));
    static_cast<void>(registry.Bind("session-two", "stream-shared", "net_ws", "ws-two"));

    EXPECT_TRUE(registry.RemoveIfPluginMatches(
        "session-one", "stream-shared", "net_ws"));
    EXPECT_FALSE(registry.Resolve("session-one", "stream-shared").has_value());
    EXPECT_TRUE(registry.Resolve("session-two", "stream-shared").has_value());
}

} // namespace
} // namespace px
