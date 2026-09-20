#include "network/relay/relay_resource_channel.h"

#include <gtest/gtest.h>

#include "px_common/data.h"
#include "px_message.pb.h"

namespace px {
namespace {

std::shared_ptr<const Data> MakePayload(const MessageType message_type) {
    Message message;
    message.set_type(message_type);
    return Data::From(message.SerializeAsString());
}

TEST(RelayResourceChannel, ClassifiesDesktopAndVoiceAudioSeparatelyFromMedia) {
    EXPECT_EQ(RelayResourceChannel::Classify(MakePayload(MessageType::kAudioFrame)), ConsoleResourceChannelKind::kAudio);
    EXPECT_EQ(RelayResourceChannel::Classify(MakePayload(MessageType::kVoiceAudioFrame)), ConsoleResourceChannelKind::kAudio);
    EXPECT_EQ(RelayResourceChannel::Classify(MakePayload(MessageType::kVideoFrame)), ConsoleResourceChannelKind::kMedia);
}

TEST(RelayResourceChannel, ClassifiesBothFileProtocolDirections) {
    EXPECT_EQ(RelayResourceChannel::Classify(MakePayload(MessageType::kFileAction)), ConsoleResourceChannelKind::kFile);
    EXPECT_EQ(RelayResourceChannel::Classify(MakePayload(MessageType::kFileResponse)), ConsoleResourceChannelKind::kFile);
}

TEST(RelayResourceChannel, UsesStableNonCollidingConnectionIds) {
    EXPECT_EQ(RelayResourceChannel::ConnectionId("relay:room", ConsoleResourceChannelKind::kMedia), "relay:room");
    EXPECT_EQ(RelayResourceChannel::ConnectionId("relay:room", ConsoleResourceChannelKind::kAudio), "relay:room:audio");
    EXPECT_EQ(RelayResourceChannel::ConnectionId("relay:room", ConsoleResourceChannelKind::kFile), "relay:room:file");
}

}  // namespace
}  // namespace px
