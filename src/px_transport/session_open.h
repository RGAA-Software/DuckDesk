#pragma once

#include <map>
#include <unordered_map>

#include "channel.h"

namespace px::transport {

// Carries the existing frontend descriptor/password parameters over the control stream.
// It does not mint credentials or authorize application traffic by itself.
using FrontendParameters = std::unordered_map<std::string, std::string>;

struct SessionOpenReply final {
    bool accepted{};
    std::string code{};
    std::string stream_id{};
    std::vector<ChannelKind> channels{ChannelKind::kControl};
};

using SessionChannels = std::map<ChannelKind, std::shared_ptr<Channel>>;

struct OpenedSession final {
    std::shared_ptr<Channel> control{};
    SessionOpenReply reply{};
    SessionChannels channels{};
};

[[nodiscard]] std::expected<Bytes, Error> EncodeSessionOpen(const FrontendParameters& parameters);
[[nodiscard]] std::expected<FrontendParameters, Error> DecodeSessionOpen(std::span<const std::uint8_t> payload);
[[nodiscard]] std::expected<Bytes, Error> EncodeSessionOpenReply(const SessionOpenReply& reply);
[[nodiscard]] std::expected<SessionOpenReply, Error> DecodeSessionOpenReply(std::span<const std::uint8_t> payload);
// Runs on a dedicated caller worker. Admission is required before returning the control stream to the SDK.
[[nodiscard]] std::expected<OpenedSession, Error> OpenFrontend(const std::shared_ptr<Connection>& connection, const FrontendParameters& parameters,
                                                               std::uint32_t timeout_ms);

}  // namespace px::transport
