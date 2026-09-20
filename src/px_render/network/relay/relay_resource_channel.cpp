#include "relay_resource_channel.h"

#include "px_common/data.h"
#include "px_message.pb.h"

namespace px {

ConsoleResourceChannelKind RelayResourceChannel::Classify(const std::shared_ptr<const Data>& payload) {
    if (!payload) {
        return ConsoleResourceChannelKind::kMedia;
    }
    Message message;
    if (!message.ParsePartialFromArray(payload->Bytes().data(), payload->Size())) {
        return ConsoleResourceChannelKind::kMedia;
    }
    switch (message.type()) {
        case MessageType::kAudioFrame:
        case MessageType::kVoiceCallRequest:
        case MessageType::kVoiceCallResponse:
        case MessageType::kVoiceAudioConfig:
        case MessageType::kVoiceAudioFrame:
            return ConsoleResourceChannelKind::kAudio;
        case MessageType::kFileAction:
        case MessageType::kFileResponse:
            return ConsoleResourceChannelKind::kFile;
        default:
            return ConsoleResourceChannelKind::kMedia;
    }
}

std::string RelayResourceChannel::ConnectionId(const std::string& connection_instance_id,
                                               const ConsoleResourceChannelKind channel_kind) {
    switch (channel_kind) {
        case ConsoleResourceChannelKind::kAudio:
            return connection_instance_id + ":audio";
        case ConsoleResourceChannelKind::kFile:
            return connection_instance_id + ":file";
        case ConsoleResourceChannelKind::kMedia:
        case ConsoleResourceChannelKind::kRdp:
            return connection_instance_id;
    }
    return connection_instance_id;
}

}  // namespace px
