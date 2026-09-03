#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>

#include <gtest/gtest.h>

#include "opus_encoder_runtime.h"
#include "px_common_new/data.h"

namespace px {
namespace {

std::shared_ptr<Data> TenMillisecondsOfSilence(int sample_rate, int channels) {
    const auto bytes = static_cast<size_t>(sample_rate / 100 * channels * 2);
    return Data::From(std::string(bytes, '\0'));
}

struct DeliveryState final {
    std::mutex mutex;
    std::condition_variable condition;
    int count = 0;
    int sample_rate = 0;
    int channels = 0;
    int bits = 0;
    int frame_size = 0;
};

TEST(OpusEncoderRuntimeTest, TenEncodeDeliverShutdownRounds) {
    for (int round = 0; round < 10; ++round) {
        auto delivery = std::make_shared<DeliveryState>();
        auto runtime = OpusEncoderRuntime::Make({});
        runtime->SetDelivery(
            [delivery](const std::shared_ptr<Data>& data,
                       int sample_rate,
                       int channels,
                       int bits,
                       int frame_size) {
                std::lock_guard lock(delivery->mutex);
                if (data && data->Size() > 0) {
                    ++delivery->count;
                }
                delivery->sample_rate = sample_rate;
                delivery->channels = channels;
                delivery->bits = bits;
                delivery->frame_size = frame_size;
                delivery->condition.notify_all();
            });
        runtime->Enqueue(TenMillisecondsOfSilence(48000, 2), 48000, 2, 16);
        runtime->Enqueue(TenMillisecondsOfSilence(48000, 2), 48000, 2, 16);
        {
            std::unique_lock lock(delivery->mutex);
            ASSERT_TRUE(delivery->condition.wait_for(
                lock, std::chrono::seconds(2), [delivery] {
                    return delivery->count == 1;
                })) << "round " << round;
            EXPECT_EQ(delivery->sample_rate, 48000);
            EXPECT_EQ(delivery->channels, 2);
            EXPECT_EQ(delivery->bits, 16);
            EXPECT_EQ(delivery->frame_size, 960);
        }
        runtime->Shutdown();
        runtime->Shutdown();
        EXPECT_FALSE(runtime->IsAccepting());
    }
}

TEST(OpusEncoderRuntimeTest, FormatChangeRecreatesEncoderAndMetadata) {
    auto delivery = std::make_shared<DeliveryState>();
    auto runtime = OpusEncoderRuntime::Make({});
    runtime->SetDelivery(
        [delivery](const std::shared_ptr<Data>&,
                   int sample_rate,
                   int channels,
                   int bits,
                   int frame_size) {
            std::lock_guard lock(delivery->mutex);
            ++delivery->count;
            delivery->sample_rate = sample_rate;
            delivery->channels = channels;
            delivery->bits = bits;
            delivery->frame_size = frame_size;
            delivery->condition.notify_all();
        });
    runtime->Enqueue(TenMillisecondsOfSilence(48000, 2), 48000, 2, 16);
    runtime->Enqueue(TenMillisecondsOfSilence(24000, 1), 24000, 1, 16);
    runtime->Enqueue(TenMillisecondsOfSilence(24000, 1), 24000, 1, 16);
    {
        std::unique_lock lock(delivery->mutex);
        ASSERT_TRUE(delivery->condition.wait_for(
            lock, std::chrono::seconds(2), [delivery] {
                return delivery->count == 1;
            }));
        EXPECT_EQ(delivery->sample_rate, 24000);
        EXPECT_EQ(delivery->channels, 1);
        EXPECT_EQ(delivery->frame_size, 480);
    }
    runtime->Shutdown();
}

TEST(OpusEncoderRuntimeTest, DeliveryCallbackCanRequestShutdownWithoutSelfJoin) {
    auto runtime = OpusEncoderRuntime::Make({});
    const auto weak_runtime = std::weak_ptr<OpusEncoderRuntime>(runtime);
    const auto deliveries = std::make_shared<std::atomic_int>(0);
    runtime->SetDelivery(
        [weak_runtime, deliveries](const std::shared_ptr<Data>&,
                                    int, int, int, int) {
            ++(*deliveries);
            if (const auto locked = weak_runtime.lock()) {
                locked->Shutdown();
            }
        });
    runtime->Enqueue(TenMillisecondsOfSilence(48000, 2), 48000, 2, 16);
    runtime->Enqueue(TenMillisecondsOfSilence(48000, 2), 48000, 2, 16);
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(2);
    while (deliveries->load() == 0 &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    runtime->Shutdown();
    EXPECT_EQ(deliveries->load(), 1);
    EXPECT_FALSE(runtime->IsAccepting());
}

TEST(OpusEncoderRuntimeTest, InvalidInputAndPostShutdownEnqueueAreIgnored) {
    auto runtime = OpusEncoderRuntime::Make({});
    const auto deliveries = std::make_shared<std::atomic_int>(0);
    runtime->SetDelivery(
        [deliveries](const std::shared_ptr<Data>&, int, int, int, int) {
            ++(*deliveries);
        });
    runtime->Enqueue(Data::From("invalid"), 0, 0, 0);
    runtime->Shutdown();
    runtime->Enqueue(TenMillisecondsOfSilence(48000, 2), 48000, 2, 16);
    EXPECT_EQ(deliveries->load(), 0);
}

}  // namespace
}  // namespace px
