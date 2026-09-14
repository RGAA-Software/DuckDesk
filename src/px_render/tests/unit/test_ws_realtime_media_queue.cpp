#include "network/ws/ws_realtime_media_queue.h"

#include "px_common/data.h"
#include "px_message.pb.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>

namespace px {
namespace {

std::shared_ptr<Data> MakeMessage(const MessageType type) {
    Message message{};
    message.set_type(type);
    return Data::From(message.SerializeAsString());
}

TEST(WsRealtimeMediaQueueTest, OnlyAudioAndVideoAreDisposableMedia) {
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kVideoFrame)), WsRealtimeMediaKind::Video);
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kAudioFrame)), WsRealtimeMediaKind::Audio);
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kVoiceAudioFrame)), WsRealtimeMediaKind::Audio);

    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kHeartBeat)), WsRealtimeMediaKind::None);
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kMouseEvent)), WsRealtimeMediaKind::None);
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kKeyEvent)), WsRealtimeMediaKind::None);
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kClipboardInfo)), WsRealtimeMediaKind::None);
    EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(MessageType::kFileAction)), WsRealtimeMediaKind::None);
    EXPECT_EQ(ClassifyWsRealtimeMedia(nullptr), WsRealtimeMediaKind::None);
    EXPECT_EQ(ClassifyWsRealtimeMedia(Data::From("not-a-protobuf-message")), WsRealtimeMediaKind::None);

    for (int value = static_cast<int>(MessageType_MIN); value <= static_cast<int>(MessageType_MAX); ++value) {
        if (!MessageType_IsValid(value)) {
            continue;
        }
        SCOPED_TRACE(value);
        const auto type = static_cast<MessageType>(value);
        auto expected = WsRealtimeMediaKind::None;
        if (type == MessageType::kVideoFrame) {
            expected = WsRealtimeMediaKind::Video;
        } else if (type == MessageType::kAudioFrame || type == MessageType::kVoiceAudioFrame) {
            expected = WsRealtimeMediaKind::Audio;
        }
        EXPECT_EQ(ClassifyWsRealtimeMedia(MakeMessage(type)), expected);
    }
}

TEST(WsRealtimeMediaQueueTest, BoundsOnlyReservedMediaWork) {
    WsRealtimeMediaQueueBudget budget{};
    for (std::size_t index{}; index < WsRealtimeMediaQueueBudget::kMaxMessages; ++index) {
        EXPECT_TRUE(budget.TryReserve(1024U));
    }
    EXPECT_FALSE(budget.TryReserve(1024U));
    EXPECT_EQ(budget.PendingMessages(), WsRealtimeMediaQueueBudget::kMaxMessages);

    for (std::size_t index{}; index < WsRealtimeMediaQueueBudget::kMaxMessages; ++index) {
        budget.Release(1024U);
    }
    EXPECT_EQ(budget.PendingMessages(), 0U);
    EXPECT_EQ(budget.PendingBytes(), 0U);
}

TEST(WsRealtimeMediaQueueTest, AllowsOneOversizedRecoveryFrameWithoutBuildingABacklog) {
    WsRealtimeMediaQueueBudget budget{};
    const auto oversized = WsRealtimeMediaQueueBudget::kMaxBytes + 1U;
    EXPECT_TRUE(budget.TryReserve(oversized));
    EXPECT_FALSE(budget.TryReserve(1U));
    budget.Release(oversized);
    EXPECT_TRUE(budget.TryReserve(1U));
}

} // namespace
} // namespace px
