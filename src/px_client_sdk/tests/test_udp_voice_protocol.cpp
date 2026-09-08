#include "px_common/px_udp_voice_protocol.h"
#include "px_common/udp_voice_send_budget.h"

#include <array>
#include <limits>
#include <gtest/gtest.h>

namespace px {
namespace {

TEST(UdpVoiceProtocol, DatagramBudgetIsBoundedAndCancellationReturnsCapacity) {
    UdpVoiceSendBudget budget{};
    std::vector<std::shared_ptr<const UdpVoiceSendBudget::Reservation>> pending{};
    for (unsigned index{}; index < UdpVoiceSendBudget::kMaximumPending; ++index) {
        pending.push_back(budget.TryAcquire());
        ASSERT_TRUE(pending.back());
    }
    EXPECT_FALSE(budget.TryAcquire());
    EXPECT_EQ(budget.Pending(), UdpVoiceSendBudget::kMaximumPending);
    pending.pop_back();
    EXPECT_EQ(budget.Pending(), UdpVoiceSendBudget::kMaximumPending - 1);
    pending.push_back(budget.TryAcquire());
    ASSERT_TRUE(pending.back());
    pending.clear();
    EXPECT_EQ(budget.Pending(), 0U);
}

TEST(UdpVoiceProtocol, CancelledIoReservationMayOutliveTheConnectionBudget) {
    auto budget = std::make_shared<UdpVoiceSendBudget>();
    auto pending = budget->TryAcquire();
    ASSERT_TRUE(pending);
    budget.reset();
    pending.reset();
    SUCCEED();
}

TEST(UdpVoiceProtocol, RoundTripOwnsPayloadAndPreservesFullTimestamp) {
    const std::array<std::uint8_t, 4> opus{0, 127, 128, 255};
    auto packet = UdpVoiceProtocol::Build("association", "call", 0xffffffffU, 0x123456789abcdef0ULL, opus);
    ASSERT_TRUE(packet);
    EXPECT_EQ(PxUdpProtocol::ParseCommon(packet->Bytes()), PxUdpProtocol::kPktVoice);
    const auto decoded = UdpVoiceProtocol::Parse(packet->Bytes());
    packet.reset();
    ASSERT_TRUE(decoded);
    EXPECT_EQ(decoded->association_code, "association");
    EXPECT_EQ(decoded->call_id, "call");
    EXPECT_EQ(decoded->sequence, std::numeric_limits<std::uint32_t>::max());
    EXPECT_EQ(decoded->capture_time_ms, 0x123456789abcdef0ULL);
    EXPECT_EQ(decoded->opus, (std::vector<std::uint8_t>{0, 127, 128, 255}));
}

TEST(UdpVoiceProtocol, StrictSingleDatagramBudgetAndIdentityBounds) {
    const std::vector<std::uint8_t> opus(UdpVoiceProtocol::kMaxOpusBytes, 1);
    EXPECT_TRUE(UdpVoiceProtocol::Build(std::string(36, 'a'), std::string(36, 'b'), 0, 0, opus));
    EXPECT_FALSE(UdpVoiceProtocol::Build(std::string(128, 'a'), std::string(128, 'b'), 0, 0, opus));
    const std::array<std::uint8_t, 1> tiny{1};
    EXPECT_TRUE(UdpVoiceProtocol::Build(std::string(128, 'a'), std::string(128, 'b'), 0, 0, tiny));
    EXPECT_FALSE(UdpVoiceProtocol::Build(std::string(129, 'a'), "call", 0, 0, tiny));
    EXPECT_FALSE(UdpVoiceProtocol::Build("association", std::string(129, 'a'), 0, 0, tiny));
    EXPECT_FALSE(UdpVoiceProtocol::Build("", "call", 0, 0, tiny));
    EXPECT_FALSE(UdpVoiceProtocol::Build("association", "", 0, 0, tiny));
    EXPECT_FALSE(UdpVoiceProtocol::Build(std::string("a\0b", 3), "call", 0, 0, tiny));
    EXPECT_FALSE(UdpVoiceProtocol::Build("association", "call", 0, 0, {}));
    EXPECT_FALSE(UdpVoiceProtocol::Build("association", "call", 0, 0, std::vector<std::uint8_t>(1276U)));
}

TEST(UdpVoiceProtocol, EveryTruncationTrailingBytesAndMalformedLengthsAreRejected) {
    const auto packet = UdpVoiceProtocol::Build("association", "call", 1, 1, std::array<std::uint8_t, 3>{1, 2, 3});
    ASSERT_TRUE(packet);
    for (std::size_t size{}; size < packet->Size(); ++size) {
        EXPECT_FALSE(UdpVoiceProtocol::Parse(packet->Bytes().first(size))) << size;
    }
    auto extra = packet->Dup();
    ASSERT_TRUE(extra->Append(std::array<char, 1>{0}));
    EXPECT_FALSE(UdpVoiceProtocol::Parse(extra->Bytes()));
    for (const auto offset : {0U, 2U, 3U, 4U, 5U, 6U, 7U}) {
        const auto invalid = packet->Dup();
        invalid->MutableBytes()[offset] = static_cast<char>(255);
        EXPECT_FALSE(UdpVoiceProtocol::Parse(invalid->Bytes())) << offset;
    }
    const auto embedded_null = packet->Dup();
    embedded_null->MutableBytes()[UdpVoiceProtocol::kHeaderSize] = 0;
    EXPECT_FALSE(UdpVoiceProtocol::Parse(embedded_null->Bytes()));
}

TEST(UdpVoiceProtocol, OrdinarySystemAudioAndControlAreNotVoice) {
    const auto audio = PxUdpProtocol::BuildAudioPacket(1, 1, std::array<char, 1>{1});
    const auto hello = PxUdpProtocol::BuildHello("association", "stream");
    ASSERT_TRUE(audio);
    ASSERT_TRUE(hello);
    EXPECT_FALSE(UdpVoiceProtocol::Parse(audio->Bytes()));
    EXPECT_FALSE(UdpVoiceProtocol::Parse(hello->Bytes()));
    const auto packet = UdpVoiceProtocol::Build("association", "call", 1, 1, std::array<std::uint8_t, 1>{1});
    PxUdpProtocol::AudioPacketInfo ordinary_audio{};
    EXPECT_FALSE(PxUdpProtocol::ParseAudioPacket(packet->Bytes(), ordinary_audio));
}

} // namespace
} // namespace px
