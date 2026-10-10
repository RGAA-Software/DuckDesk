#include <gtest/gtest.h>

#include <future>

#include "px_transport/paced_media_batch.h"

namespace px {
namespace {

struct BatchTestState final {
    std::size_t packets_sent{};
    std::size_t packets_when_control_ran{};
    bool cancelled{};
    std::size_t completions{};
    bool succeeded{};
};

TEST(PacedMediaBatch, SocketExecutorProcessesControlBetweenVideoBursts) {
    asio::io_context executor{};
    const auto state = std::make_shared<BatchTestState>();
    const auto control_timer = std::make_shared<asio::steady_timer>(executor);
    const auto batch = std::make_shared<PacedMediaBatch>(
        executor.get_executor(), std::vector<media::Packet>(512, media::Packet(1400)), false,
        [state, control_timer](std::span<const media::Packet> packets) {
            EXPECT_LE(packets.size(), media::UdpVideoBurstPacing::kPacketsPerBatch);
            if (state->packets_sent == 0) {
                control_timer->expires_after(std::chrono::milliseconds(1));
                control_timer->async_wait([state](const asio::error_code& error) {
                    EXPECT_FALSE(error);
                    state->packets_when_control_ran = state->packets_sent;
                });
            }
            state->packets_sent += packets.size();
            return true;
        },
        [] { return true; },
        [state](bool succeeded) {
            ++state->completions;
            state->succeeded = succeeded;
        });
    asio::post(executor, [batch] { batch->Start(); });
    executor.run();
    EXPECT_GT(state->packets_when_control_ran, 0);
    EXPECT_LT(state->packets_when_control_ran, 512);
    EXPECT_EQ(state->packets_sent, 512);
    EXPECT_TRUE(state->succeeded);
    EXPECT_EQ(state->completions, 1);
}

TEST(PacedMediaBatch, CancellationBetweenBurstsStopsRemainingPacketsAndCompletesOnce) {
    asio::io_context executor{};
    const auto state = std::make_shared<BatchTestState>();
    auto batch = std::make_shared<PacedMediaBatch>(
        executor.get_executor(), std::vector<media::Packet>(512, media::Packet(1400)), false,
        [state, executor_handle = executor.get_executor()](std::span<const media::Packet> packets) {
            state->packets_sent += packets.size();
            asio::post(executor_handle, [state] { state->cancelled = true; });
            return true;
        },
        [state] { return !state->cancelled; },
        [state](bool succeeded) {
            ++state->completions;
            state->succeeded = succeeded;
        });
    asio::post(executor, [batch] { batch->Start(); });
    executor.run();
    batch.reset();
    EXPECT_EQ(state->packets_sent, media::UdpVideoBurstPacing::kPacketsPerBatch);
    EXPECT_FALSE(state->succeeded);
    EXPECT_EQ(state->completions, 1);
}

TEST(PacedMediaBatch, AbandoningExecutorReleasesQueuedBatchAndCompletesFailure) {
    const auto state = std::make_shared<BatchTestState>();
    {
        asio::io_context executor{};
        auto batch = std::make_shared<PacedMediaBatch>(
            executor.get_executor(), std::vector<media::Packet>(512, media::Packet(1400)), false,
            [state](std::span<const media::Packet> packets) {
                state->packets_sent += packets.size();
                return true;
            },
            [] { return true; },
            [state](bool succeeded) {
                ++state->completions;
                state->succeeded = succeeded;
            });
        batch->Start();
        batch.reset();
    }
    EXPECT_FALSE(state->succeeded);
    EXPECT_EQ(state->completions, 1);
    EXPECT_EQ(state->packets_sent, media::UdpVideoBurstPacing::kPacketsPerBatch);
}

}  // namespace
}  // namespace px
