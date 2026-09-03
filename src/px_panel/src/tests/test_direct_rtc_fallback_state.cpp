#include <gtest/gtest.h>

#include "render_panel/devices/direct_rtc_fallback_state.h"
#include "px_client_panel_message.pb.h"

namespace px {

TEST(DirectRtcFallbackState, FreshDirectAttemptAllowsFallback) {
    DirectRtcFallbackState state;
    EXPECT_TRUE(state.ShouldFallback());
}

TEST(DirectRtcFallbackState, PanelChannelAloneDoesNotSuppressFallback) {
    DirectRtcFallbackState state;
    state.MarkPanelChannelConnected();
    EXPECT_TRUE(state.IsPanelChannelConnected());
    EXPECT_TRUE(state.ShouldFallback());
}

TEST(DirectRtcFallbackState, RemoteTransportSuppressesFallback) {
    DirectRtcFallbackState state;
    state.MarkPanelChannelConnected();
    state.MarkTransportConnected();
    EXPECT_FALSE(state.ShouldFallback());
}

TEST(DirectRtcFallbackState, TerminalRejectionSuppressesFallback) {
    DirectRtcFallbackState state;
    state.MarkPanelChannelConnected();
    state.MarkTerminalRejected();
    EXPECT_FALSE(state.ShouldFallback());
}

TEST(DirectRtcFallbackState, TransportReadyProtocolRoundTrips) {
    pxcp::CpMessage outgoing;
    outgoing.set_type(pxcp::CpMessageType::kCpTransportConnected);
    outgoing.set_stream_id("rtc-direct-attempt");

    pxcp::CpMessage incoming;
    ASSERT_TRUE(incoming.ParseFromString(outgoing.SerializeAsString()));
    EXPECT_EQ(incoming.type(), pxcp::CpMessageType::kCpTransportConnected);
    EXPECT_EQ(incoming.stream_id(), "rtc-direct-attempt");
}

TEST(DirectRtcFallbackState, TransportRejectionProtocolRoundTrips) {
    pxcp::CpMessage outgoing;
    outgoing.set_type(pxcp::CpMessageType::kCpTransportRejected);
    outgoing.set_stream_id("ip-direct-attempt");
    outgoing.mutable_transport_rejected()->set_reason(
        pxcp::CpTransportRejection::kCpRejectionAuthorization);

    pxcp::CpMessage incoming;
    ASSERT_TRUE(incoming.ParseFromString(outgoing.SerializeAsString()));
    EXPECT_EQ(incoming.type(), pxcp::CpMessageType::kCpTransportRejected);
    EXPECT_EQ(incoming.stream_id(), "ip-direct-attempt");
    EXPECT_EQ(incoming.transport_rejected().reason(),
              pxcp::CpTransportRejection::kCpRejectionAuthorization);
}

TEST(DirectRtcFallbackState, TenRepeatedAttemptsDoNotLeakReadiness) {
    for (int round = 0; round < 10; ++round) {
        DirectRtcFallbackState state;
        state.MarkPanelChannelConnected();
        EXPECT_TRUE(state.ShouldFallback()) << "round=" << round;
        state.MarkTransportConnected();
        EXPECT_FALSE(state.ShouldFallback()) << "round=" << round;
    }
}

} // namespace px
