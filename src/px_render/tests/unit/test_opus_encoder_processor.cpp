#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

#include "processors/opus_encoder_processor.h"

namespace px::render {
namespace {

using namespace std::chrono_literals;

struct EncodedState final {
    std::mutex mutex;
    std::condition_variable condition;
    std::uint64_t bus_packets{0};
    std::uint64_t network_packets{0};
    std::uint32_t frame_size{0};
};

std::shared_ptr<const CapturedAudioFrame> MakePcm() {
    return std::make_shared<const CapturedAudioFrame>(CapturedAudioFrame{
        .timestamp_us = 1,
        .sample_rate_hz = 48000,
        .channels = 2,
        .bits_per_sample = 16,
        .payload = MakeImmutableByteBuffer(std::string(480 * 2 * 2, '\0')),
    });
}

TEST(OpusEncoderProcessorTest, TenStartEncodeStopRoundsAreComplete) {
    const auto bus = EncodedMediaBus::Create();
    const auto state = std::make_shared<EncodedState>();
    const auto callback =
        std::make_shared<EncodedMediaBus::EncodedAudioCallback>(
            [state](const std::shared_ptr<const EncodedAudioFrame>& frame) {
                std::lock_guard lock(state->mutex);
                ++state->bus_packets;
                state->frame_size = frame ? frame->frame_size : 0;
                state->condition.notify_all();
            });
    const auto subscription = bus->SubscribeEncodedAudio(callback);
    const auto processor = OpusEncoderProcessor::Create(
        bus,
        [state](const std::shared_ptr<const EncodedAudioFrame>& frame) {
            if (!frame) {
                return;
            }
            std::lock_guard lock(state->mutex);
            ++state->network_packets;
            state->condition.notify_all();
        });
    ASSERT_TRUE(processor);

    for (std::uint64_t round = 0; round < 10; ++round) {
        ASSERT_TRUE(processor->Start());
        bus->PublishCapturedAudio(MakePcm());
        bus->PublishCapturedAudio(MakePcm());
        {
            std::unique_lock lock(state->mutex);
            ASSERT_TRUE(state->condition.wait_for(lock, 2s, [state, round] {
                return state->bus_packets >= round + 1 &&
                    state->network_packets >= round + 1;
            })) << "round " << round;
            EXPECT_EQ(state->frame_size, 960U);
        }
        ASSERT_TRUE(processor->Stop());
    }
    EXPECT_EQ(processor->Snapshot().input_packets, 20U);
    EXPECT_EQ(processor->Snapshot().output_packets, 10U);
    subscription->Reset();
}

TEST(OpusEncoderProcessorTest, StopUnregistersCapturedAudioCallback) {
    const auto bus = EncodedMediaBus::Create();
    auto processor = OpusEncoderProcessor::Create(bus);
    ASSERT_TRUE(processor->Start());
    ASSERT_TRUE(bus->NeedsCapturedAudio());
    ASSERT_TRUE(processor->Stop());
    EXPECT_FALSE(bus->NeedsCapturedAudio());
    processor.reset();
    bus->PublishCapturedAudio(MakePcm());
}

}  // namespace
}  // namespace px::render
