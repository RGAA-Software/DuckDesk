#include <string>
#include <string_view>

#include <gtest/gtest.h>

#include "px_render/network/webrtc/local/rtc_candidate_sdp.h"

namespace px {
namespace {

constexpr std::string_view kAnswer = "v=0\r\n"
                                     "m=video 9 UDP/TLS/RTP/SAVPF 96\r\n"
                                     "a=candidate:host-a 1 udp 2122260223 192.168.1.10 5000 typ host generation 0\r\n"
                                     "a=candidate:tcp-a 1 tcp 1518280447 192.168.1.10 9 typ host tcptype active generation 0\r\n"
                                     "a=candidate:srflx-a 1 udp 1686052607 198.51.100.10 5001 typ srflx raddr 192.168.1.10 rport 5001\r\n";

TEST(RtcCandidateSdp, EmptyOrInvalidAdvertisedAddressLeavesAnswerUntouched) {
    EXPECT_EQ(AddAdvertisedIpv4HostCandidates({}, std::string{kAnswer}), kAnswer);
    EXPECT_EQ(AddAdvertisedIpv4HostCandidates("render.example.com", std::string{kAnswer}), kAnswer);
    EXPECT_EQ(AddAdvertisedIpv4HostCandidates("0.0.0.0", std::string{kAnswer}), kAnswer);
}

TEST(RtcCandidateSdp, AddsOnlyPublicUdpHostEquivalentWithoutChangingOriginalCandidates) {
    const auto answer = AddAdvertisedIpv4HostCandidates("203.0.113.8", std::string{kAnswer});

    EXPECT_NE(answer.find("a=candidate:host-a 1 udp 2122260223 192.168.1.10 5000 typ host"), std::string::npos);
    EXPECT_NE(answer.find("a=candidate:pxphost-a 1 udp 2122260224 203.0.113.8 5000 typ host"), std::string::npos);
    EXPECT_EQ(answer.find("a=candidate:tcp-a 1 tcp 1518280447 203.0.113.8"), std::string::npos);
    EXPECT_EQ(answer.find("a=candidate:srflx-a 1 udp 1686052607 203.0.113.8 5001 typ srflx"), std::string::npos);
}

} // namespace
} // namespace px
