#include "media_transport/video_packetizer.h"
#include "media_transport/video_receive_queue.h"
#include "upstream_video_oracle.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <numeric>
#include <random>

namespace px::media::test {
namespace {
Packet Frame(std::size_t size) {
    Packet frame(size);
    for (std::size_t index{}; index < size; ++index)
        frame[index] = static_cast<std::uint8_t>(index % 251);
    return frame;
}
Packet Payload(const OracleResults& results, std::size_t size) {
    Packet output{};
    for (const auto& packet : results.data_packets) {
        if (packet.size() < 32)
            return {};
        output.insert(output.end(), packet.begin() + 32, packet.end());
    }
    if (output.size() < size + 8)
        return {};
    return Packet(output.begin() + 8, output.begin() + static_cast<std::ptrdiff_t>(8 + size));
}
std::uint32_t Little32(const Packet& packet, std::size_t offset) {
    std::uint32_t value{};
    for (std::size_t index{}; index < 4; ++index)
        value |= static_cast<std::uint32_t>(packet[offset + index]) << (index * 8);
    return value;
}
} // namespace

TEST(UpstreamMediaReference, RealMoonlightQueueAcceptsSunshinePacketLayout) {
    for (const std::size_t size : {500, 20000, 300000, 700000, 1200000}) {
        SCOPED_TRACE(size);
        const auto original = Frame(size);
        const auto encoded = PacketizeVideo(original, {});
        ASSERT_TRUE(encoded);
        UpstreamVideoOracle oracle(1400);
        for (const auto& packet : encoded->packets)
            oracle.Feed(packet);
        EXPECT_EQ(Payload(oracle.Results(), original.size()), original);
        EXPECT_TRUE(oracle.Results().losses.empty());
    }
}

TEST(UpstreamMediaReference, StatisticsSeparateRealPacketsRecoveryDuplicatesAndReset) {
    const auto encoded = PacketizeVideo(Frame(10000), {});
    ASSERT_TRUE(encoded);
    VideoReceiveQueue receiver(1400);
    static_cast<void>(receiver.Feed(encoded->packets[1], 1));
    static_cast<void>(receiver.Feed(encoded->packets[1], 2));
    static_cast<void>(receiver.Feed(encoded->packets[0], 3));
    EXPECT_EQ(receiver.Statistics().duplicates, 1U);
    EXPECT_EQ(receiver.Statistics().reordered_packets, 1U);
    EXPECT_EQ(receiver.Statistics().data_packets, 3U);
    receiver.Reset();
    EXPECT_EQ(receiver.Statistics().data_packets, 0U);
    for (std::size_t index{1}; index < encoded->packets.size(); ++index)
        static_cast<void>(receiver.Feed(encoded->packets[index], index));
    EXPECT_EQ(receiver.Statistics().completed_frames, 1U);
    EXPECT_EQ(receiver.Statistics().recovered_data, 1U);
    EXPECT_EQ(receiver.Statistics().data_packets + receiver.Statistics().parity_packets, encoded->packets.size() - 1);
    EXPECT_GT(receiver.Statistics().late_packets, 0U);
    static_cast<void>(receiver.Feed({}, 100));
    EXPECT_EQ(receiver.Statistics().malformed_packets, 1U);
}

TEST(UpstreamMediaReference, StatisticsCountSkippedFramesOnlyAfterJoiningAndCorrectFalsePrediction) {
    VideoPacketParameters parameters{};
    parameters.frame_index = 100;
    const auto first = PacketizeVideo(Frame(10000), parameters);
    ASSERT_TRUE(first);
    VideoReceiveQueue receiver(1400);
    for (const auto& packet : first->packets)
        static_cast<void>(receiver.Feed(packet, 1));
    EXPECT_EQ(receiver.Statistics().unrecoverable_frames, 0U);
    parameters.frame_index = 103;
    const auto next = PacketizeVideo(Frame(10000), parameters);
    ASSERT_TRUE(next);
    // First observation of a high shard predicts loss; late earlier shards correct that prediction.
    static_cast<void>(receiver.Feed(next->packets[5], 2));
    for (std::size_t index{}; index < next->packets.size(); ++index)
        static_cast<void>(receiver.Feed(next->packets[index], 3 + index));
    EXPECT_EQ(receiver.Statistics().unrecoverable_frames, 2U);
    EXPECT_EQ(receiver.Statistics().predicted_losses, 1U);
    EXPECT_EQ(receiver.Statistics().prediction_corrections, 1U);
    EXPECT_EQ(receiver.Statistics().completed_frames, 2U);
}

TEST(UpstreamMediaReference, RealMoonlightRecoversFullParityBudgetInEveryBlock) {
    const auto original = Frame(500000);
    const auto encoded = PacketizeVideo(original, {});
    ASSERT_TRUE(encoded);
    ASSERT_GT(encoded->block_count, 1);
    for (std::uint32_t seed{}; seed < 16; ++seed) {
        SCOPED_TRACE(seed);
        UpstreamVideoOracle oracle(1400);
        VideoReceiveQueue receiver(1400);
        OracleResults actual{};
        for (std::uint8_t block{}; block < encoded->block_count; ++block) {
            std::vector<Packet> packets{};
            for (const auto& packet : encoded->packets) {
                if (((packet[27] >> 4) & 3) == block)
                    packets.push_back(packet);
            }
            ASSERT_FALSE(packets.empty());
            const auto data_count = Little32(packets.front(), 28) >> 22;
            std::mt19937 random(seed);
            std::shuffle(packets.begin(), packets.end(), random);
            for (std::size_t index{}; index < data_count; ++index) {
                oracle.Feed(packets[index]);
                auto result = receiver.Feed(packets[index], 0);
                for (auto& data : result.data_packets)
                    actual.data_packets.push_back(std::move(data));
                for (const auto& loss : result.losses)
                    actual.losses.push_back({loss.frame_index, loss.speculative});
            }
        }
        EXPECT_EQ(Payload(oracle.Results(), original.size()), original);
        EXPECT_EQ(Payload(actual, original.size()), original);
        const auto expected = oracle.Results();
        ASSERT_EQ(actual.losses.size(), expected.losses.size());
        for (std::size_t index{}; index < actual.losses.size(); ++index) {
            EXPECT_EQ(actual.losses[index].frame, expected.losses[index].frame);
            EXPECT_EQ(actual.losses[index].speculative, expected.losses[index].speculative);
        }
    }
}

TEST(UpstreamMediaReference, MultiFrameLossPredictionAndDeliveryMatchUpstream) {
    for (std::uint32_t seed{}; seed < 32; ++seed) {
        SCOPED_TRACE(seed);
        UpstreamVideoOracle oracle(1400);
        VideoReceiveQueue receiver(1400);
        OracleResults actual{};
        std::mt19937 random(seed);
        std::uint16_t sequence{};
        for (std::uint32_t frame = 1; frame <= 8; ++frame) {
            const auto encoded = PacketizeVideo(Frame(20000), {.frame_index = frame, .timestamp_90khz = frame * 1500, .sequence = sequence});
            ASSERT_TRUE(encoded);
            sequence = encoded->next_sequence;
            if ((seed + frame) % 9 == 0)
                continue; // Entire frame absent.
            auto packets = encoded->packets;
            if (seed % 2 == 0)
                std::shuffle(packets.begin(), packets.end(), random);
            for (const auto& packet : packets) {
                if (random() % 5 == 0)
                    continue;
                oracle.Feed(packet);
                auto result = receiver.Feed(packet, frame * 16667);
                for (auto& data : result.data_packets)
                    actual.data_packets.push_back(std::move(data));
                for (const auto& loss : result.losses)
                    actual.losses.push_back({loss.frame_index, loss.speculative});
            }
        }
        const auto expected = oracle.Results();
        ASSERT_EQ(actual.data_packets.size(), expected.data_packets.size());
        for (std::size_t index{}; index < actual.data_packets.size(); ++index) {
            const auto& received = actual.data_packets[index];
            const auto& reference = expected.data_packets[index];
            EXPECT_TRUE(std::equal(received.begin() + 32, received.end(), reference.begin() + 32, reference.end()));
        }
        ASSERT_EQ(actual.losses.size(), expected.losses.size());
        for (std::size_t index{}; index < actual.losses.size(); ++index) {
            EXPECT_EQ(actual.losses[index].frame, expected.losses[index].frame);
            EXPECT_EQ(actual.losses[index].speculative, expected.losses[index].speculative);
        }
    }
}

TEST(UpstreamMediaReference, LargeFrameFallbackMatchesFourBlockProtocolLimit) {
    const auto encoded = PacketizeVideo(Frame(1200000), {});
    ASSERT_TRUE(encoded);
    EXPECT_EQ(encoded->block_count, 4);
    EXPECT_TRUE(encoded->fec_disabled_for_large_frame);
    for (const auto& packet : encoded->packets)
        EXPECT_EQ((Little32(packet, 28) >> 4) & 255, 0);
}

TEST(UpstreamMediaReference, AudioParityMatchesRealMoonlightAudioInitialization) {
    std::vector<Packet> shards(6, Packet(160));
    for (std::size_t index{}; index < 4; ++index) {
        for (std::size_t byte{}; byte < 160; ++byte)
            shards[index][byte] = static_cast<std::uint8_t>((byte + index * 17) % 251);
    }
    ASSERT_TRUE(NanorsCodec::Encode(shards, 4, FecProfile::kAudio4Plus2));
    const auto expected = shards;
    for (std::size_t first{}; first < 6; ++first) {
        for (std::size_t second = first + 1; second < 6; ++second) {
            auto damaged = expected;
            std::vector<std::uint8_t> missing(6, 0);
            missing[first] = missing[second] = 1;
            damaged[first].assign(160, 0);
            damaged[second].assign(160, 0);
            ASSERT_TRUE(DecodeWithUpstreamAudioMatrix(damaged, missing));
            for (std::size_t index{}; index < 4; ++index)
                EXPECT_EQ(damaged[index], expected[index]);
        }
    }
}

TEST(UpstreamMediaReference, CodecRecoversProtectedMetadataAndRejectsOverBudget) {
    std::vector<Packet> shards{Frame(128), Frame(128), Packet(128), Packet(128)};
    shards[1][0] = 78;
    ASSERT_TRUE(NanorsCodec::Encode(shards, 2));
    const auto expected = shards;
    shards[0].assign(128, 0);
    shards[1].assign(128, 0);
    const std::vector<std::uint8_t> missing{1, 1, 0, 0};
    ASSERT_TRUE(NanorsCodec::Decode(shards, missing, 2));
    EXPECT_EQ(shards[0], expected[0]);
    EXPECT_EQ(shards[1], expected[1]);
    EXPECT_FALSE(NanorsCodec::Decode(shards, std::vector<std::uint8_t>{1, 1, 1, 0}, 2));
}
} // namespace px::media::test
