#include "media_transport/audio_stream.h"
#include "media_transport/video_stream.h"
#include "upstream_video_oracle.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <random>

namespace px::media::test {
TEST(MediaStream, ProtectedVideoMetadataSurvivesLossOfFirstShard) {
    for (const std::size_t size : {2000, 350000, 700000}) {
        VideoFrame frame{};
        frame.kind = VideoFrameKind::kIdr;
        frame.codec = VideoCodec::kH265;
        frame.stream = 3;
        frame.monitor = "monitor-3";
        frame.width = 1920;
        frame.height = 1080;
        frame.frame_index = 197;
        frame.encoded.assign(size, 0x79);
        const auto packets = PacketizeVideoFrame(frame, {});
        ASSERT_TRUE(packets);
        VideoStreamReceiver receiver{};
        std::optional<VideoFrame> delivered{};
        for (std::size_t index = 1; index < packets->packets.size(); ++index) {
            const auto datagram = ParseMedia(packets->packets[index]);
            ASSERT_TRUE(datagram);
            auto output = receiver.Feed(*datagram, 1000000);
            if (output.frame)
                delivered = std::move(output.frame);
        }
        ASSERT_TRUE(delivered);
        EXPECT_EQ(delivered->encoded, frame.encoded);
        EXPECT_EQ(delivered->monitor, frame.monitor);
        EXPECT_EQ(delivered->frame_index, frame.frame_index);
        EXPECT_EQ(delivered->codec, frame.codec);
        EXPECT_EQ(delivered->width, frame.width);
    }
}
TEST(MediaStream, ReferenceChainRejectsGapUntilIdrAndResetClearsStreams) {
    VideoFrame frame{};
    frame.width = frame.height = 100;
    frame.encoded.assign(200, 1);
    VideoStreamReceiver receiver{};
    const auto feed = [&receiver](const VideoFrame& input) {
        VideoPacketParameters parameters{};
        parameters.frame_index = static_cast<std::uint32_t>(input.frame_index + 1);
        const auto packets = PacketizeVideoFrame(input, parameters);
        std::size_t delivered{};
        if (!packets)
            return delivered;
        for (const auto& packet : packets->packets) {
            const auto datagram = ParseMedia(packet);
            if (datagram && receiver.Feed(*datagram, 1000000).frame)
                ++delivered;
        }
        return delivered;
    };
    EXPECT_EQ(feed(frame), 0);
    ++frame.frame_index;
    frame.kind = VideoFrameKind::kIdr;
    EXPECT_EQ(feed(frame), 1);
    frame.frame_index += 2;
    frame.kind = VideoFrameKind::kPredicted;
    EXPECT_EQ(feed(frame), 0);
    ++frame.frame_index;
    EXPECT_EQ(feed(frame), 0);
    ++frame.frame_index;
    frame.kind = VideoFrameKind::kIdr;
    EXPECT_EQ(feed(frame), 1);
    EXPECT_EQ(feed(frame), 0);
    receiver.Reset();
    EXPECT_EQ(feed(frame), 1);
}
TEST(MediaStream, CaptureTimestampGapsAreNotTransportLossAndRecoveryUsesEncoderTimestamp) {
    VideoStreamReceiver receiver{};
    VideoFrame frame{};
    frame.width = frame.height = 100;
    frame.encoded.assign(200, 1);
    frame.frame_index = 1000000000000ULL;
    frame.kind = VideoFrameKind::kIdr;
    VideoPacketParameters parameters{};
    parameters.frame_index = 400;
    const auto feed = [&receiver, &frame, &parameters]() {
        auto packets = PacketizeVideoFrame(frame, parameters);
        EXPECT_TRUE(packets);
        VideoStreamOutput result{};
        if (!packets)
            return result;
        parameters.sequence = packets->next_sequence;
        for (const auto& packet : packets->packets) {
            const auto datagram = ParseMedia(packet);
            EXPECT_TRUE(datagram);
            if (!datagram)
                continue;
            auto output = receiver.Feed(*datagram, 1000000);
            if (output.frame)
                result.frame = std::move(output.frame);
            if (output.invalid_reference_frame)
                result.invalid_reference_frame = output.invalid_reference_frame;
            result.needs_idr |= output.needs_idr;
            result.losses.insert(result.losses.end(), output.losses.begin(), output.losses.end());
        }
        return result;
    };
    auto first = feed();
    ASSERT_TRUE(first.frame);
    EXPECT_TRUE(first.losses.empty()); // Late joining must not invalidate frames before the decoder's initial reference point.
    frame.frame_index += 42;
    frame.kind = VideoFrameKind::kPredicted;
    ++parameters.frame_index;
    auto skipped_capture = feed();
    ASSERT_TRUE(skipped_capture.frame);
    EXPECT_EQ(skipped_capture.frame->frame_index, frame.frame_index);
    EXPECT_TRUE(skipped_capture.losses.empty());
    EXPECT_FALSE(skipped_capture.needs_idr);
    const auto invalid_timestamp = frame.frame_index + 1;
    parameters.frame_index += 2;
    frame.frame_index += 17;
    auto lost_transport = feed();
    EXPECT_FALSE(lost_transport.frame);
    EXPECT_FALSE(lost_transport.needs_idr);
    ASSERT_TRUE(lost_transport.invalid_reference_frame);
    EXPECT_EQ(*lost_transport.invalid_reference_frame, invalid_timestamp);
}

TEST(MediaStream, CompleteRecoveryDoesNotRequestAnotherInvalidationForTheSameGap) {
    for (const auto recovery : {VideoFrameKind::kIdr, VideoFrameKind::kReferenceRecovery}) {
        VideoStreamReceiver receiver{};
        VideoFrame frame{};
        frame.width = frame.height = 100;
        frame.encoded.assign(200, 1);
        frame.frame_index = 100;
        frame.kind = VideoFrameKind::kIdr;
        VideoPacketParameters parameters{};
        parameters.frame_index = 1;
        const auto initial = PacketizeVideoFrame(frame, parameters);
        ASSERT_TRUE(initial);
        const auto first = ParseMedia(initial->packets.front());
        ASSERT_TRUE(first);
        ASSERT_TRUE(receiver.Feed(*first, 1000000).frame);
        parameters.sequence = initial->next_sequence;
        parameters.frame_index = 3; // Frame 2 was completely lost.
        frame.frame_index = 102;
        frame.kind = recovery;
        const auto packets = PacketizeVideoFrame(frame, parameters);
        ASSERT_TRUE(packets);
        const auto datagram = ParseMedia(packets->packets.front());
        ASSERT_TRUE(datagram);
        const auto output = receiver.Feed(*datagram, 1100000);
        ASSERT_TRUE(output.frame);
        EXPECT_FALSE(output.losses.empty());
        EXPECT_FALSE(output.needs_idr);
        EXPECT_FALSE(output.invalid_reference_frame);
    }
}

TEST(MediaStream, PacketAndFrameCountersWrapWithoutDuplicateDeliveryAcrossIndependentStreams) {
    VideoStreamReceiver receiver{};
    std::array<std::uint16_t, 2> sequences{65534, 65534};
    for (const auto wire_frame : {1U, 0x70000000U, 0xE0000000U, 0xFFFFFFFFU, 0U, 1U}) {
        for (std::uint8_t stream{}; stream < 2; ++stream) {
            VideoFrame frame{};
            frame.width = frame.height = 100;
            frame.stream = stream;
            frame.kind = VideoFrameKind::kIdr;
            frame.frame_index = wire_frame;
            frame.monitor = std::to_string(stream);
            frame.encoded.assign(200, stream);
            VideoPacketParameters parameters{};
            parameters.frame_index = wire_frame;
            parameters.sequence = sequences[stream];
            const auto packets = PacketizeVideoFrame(frame, parameters);
            ASSERT_TRUE(packets);
            sequences[stream] = packets->next_sequence;
            unsigned delivered{};
            for (const auto& packet : packets->packets) {
                const auto datagram = ParseMedia(packet);
                ASSERT_TRUE(datagram);
                const auto result = receiver.Feed(*datagram, 1000000);
                if (result.frame) {
                    ++delivered;
                    EXPECT_EQ(result.frame->encoded, frame.encoded);
                    EXPECT_EQ(result.frame->stream, stream);
                }
                EXPECT_FALSE(receiver.Feed(*datagram, 1000001).frame);
            }
            EXPECT_EQ(delivered, 1U);
        }
    }
}

TEST(MediaStream, LostRequestAndLostRecoveryNeverReleaseDependentFrames) {
    VideoStreamReceiver receiver{};
    VideoFrame frame{};
    frame.width = frame.height = 100;
    frame.encoded.assign(200, 1);
    VideoPacketParameters parameters{};
    const auto feed = [&receiver, &frame, &parameters](std::uint32_t index, VideoFrameKind kind) {
        frame.frame_index = index * 2;
        frame.kind = kind;
        parameters.frame_index = index;
        const auto packets = PacketizeVideoFrame(frame, parameters);
        EXPECT_TRUE(packets);
        if (!packets)
            return VideoStreamOutput{};
        parameters.sequence = packets->next_sequence;
        const auto datagram = ParseMedia(packets->packets.front());
        EXPECT_TRUE(datagram);
        return datagram ? receiver.Feed(*datagram, 1000000 + index * 20000) : VideoStreamOutput{};
    };
    ASSERT_TRUE(feed(1, VideoFrameKind::kIdr).frame);
    for (const auto index : {3U, 4U, 6U}) { // Frame 2, the first request, and recovery frame 5 are lost.
        const auto result = feed(index, VideoFrameKind::kPredicted);
        EXPECT_FALSE(result.frame);
        EXPECT_EQ(result.invalid_reference_frame, 3U);
    }
    ASSERT_TRUE(feed(7, VideoFrameKind::kReferenceRecovery).frame);
    ASSERT_TRUE(feed(8, VideoFrameKind::kPredicted).frame);
}

TEST(MediaStream, AudioQueueMatchesPristineMoonlightWithLossReorderingAndPlc) {
    for (std::uint32_t seed{}; seed < 64; ++seed) {
        SCOPED_TRACE(seed);
        std::mt19937 random(seed);
        AudioPacketizer sender{};
        AudioReceiveQueue receiver{};
        UpstreamAudioOracle oracle{};
        std::uint64_t clock_us = 1000000;
        std::size_t deliveries{};
        for (std::size_t group{}; group < 24; ++group) {
            std::vector<Packet> packets{};
            for (std::size_t index{}; index < 4; ++index) {
                const Packet opus(20 + random() % 300, static_cast<std::uint8_t>(group * 4 + index));
                auto encoded = sender.Push(opus, 1400);
                for (const auto& packet : encoded) {
                    const auto datagram = ParseMedia(packet);
                    ASSERT_TRUE(datagram);
                    packets.emplace_back(datagram->payload.begin(), datagram->payload.end());
                }
            }
            if (seed % 2 == 0)
                std::shuffle(packets.begin(), packets.end(), random);
            for (const auto& packet : packets) {
                clock_us += 15000;
                if (random() % 100 < 25)
                    continue;
                const auto expected = oracle.Feed(packet, clock_us);
                const auto actual = receiver.Feed(packet, clock_us);
                ASSERT_EQ(actual.packets.size(), expected.size());
                for (std::size_t index{}; index < expected.size(); ++index) {
                    EXPECT_EQ(actual.packets[index].sequence, expected[index].sequence);
                    EXPECT_EQ(actual.packets[index].payload, expected[index].payload);
                    ++deliveries;
                }
            }
        }
        EXPECT_GT(deliveries, 0);
    }
}
TEST(MediaStream, AudioRecoversEveryTwoErasureCombinationAndDeliversInOrderImmediately) {
    for (std::size_t first{}; first < 6; ++first) {
        for (std::size_t second = first + 1; second < 6; ++second) {
            AudioPacketizer sender{};
            AudioReceiveQueue receiver{};
            std::vector<Packet> group{};
            for (std::size_t index{}; index < 8; ++index) {
                auto packets = sender.Push(Packet(40 + index, static_cast<std::uint8_t>(index)), 1400);
                for (const auto& packet : packets) {
                    const auto datagram = ParseMedia(packet);
                    ASSERT_TRUE(datagram);
                    if (index < 4) {
                        EXPECT_TRUE(receiver.Feed(datagram->payload, 1000000).packets.empty());
                    } else {
                        group.emplace_back(datagram->payload.begin(), datagram->payload.end());
                    }
                }
            }
            std::vector<AudioDelivery> deliveries{};
            for (std::size_t index{}; index < group.size(); ++index) {
                if (index == first || index == second)
                    continue;
                auto output = receiver.Feed(group[index], 1080000);
                deliveries.insert(deliveries.end(), output.packets.begin(), output.packets.end());
            }
            ASSERT_EQ(deliveries.size(), 4);
            for (std::size_t index{}; index < 4; ++index) {
                EXPECT_EQ(deliveries[index].sequence, index + 4);
                const auto opus = UnwrapAudioPayload(deliveries[index].payload);
                ASSERT_TRUE(opus);
                EXPECT_EQ(*opus, Packet(44 + index, static_cast<std::uint8_t>(index + 4)));
            }
        }
    }
}
} // namespace px::media::test
