#include "session_open.h"

#include <chrono>
#include <nlohmann/json.hpp>

namespace px::transport {
namespace {
constexpr std::size_t kMaximumHandshakeBytes{16384};
constexpr std::size_t kMaximumParameters{32};

std::expected<Bytes, Error> EncodeJson(const nlohmann::json& document) {
    const auto text = document.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
    if (text.size() > kMaximumHandshakeBytes) return std::unexpected(Error::kInvalid);
    return Bytes(text.begin(), text.end());
}

bool ValidParameters(const FrontendParameters& parameters) {
    if (parameters.empty() || parameters.size() > kMaximumParameters) return false;
    for (const auto& [name, value] : parameters) {
        if (name.empty() || name.size() > 128 || value.size() > 8192) return false;
    }
    const auto stream = parameters.find("stream_id");
    return stream != parameters.end() && !stream->second.empty();
}

bool ValidChannels(const std::vector<ChannelKind>& channels) {
    if (channels.empty() || channels.size() > 5 || channels.front() != ChannelKind::kControl) return false;
    std::uint8_t previous{};
    for (const auto kind : channels) {
        const auto value = static_cast<std::uint8_t>(kind);
        if (value <= previous || value > static_cast<std::uint8_t>(ChannelKind::kRdp)) return false;
        previous = value;
    }
    return true;
}

std::uint32_t RemainingMilliseconds(std::chrono::steady_clock::time_point deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    return remaining > 0 ? static_cast<std::uint32_t>(remaining) : 0;
}
}  // namespace

std::expected<Bytes, Error> EncodeSessionOpen(const FrontendParameters& parameters) {
    if (!ValidParameters(parameters)) return std::unexpected(Error::kInvalid);
    return EncodeJson(nlohmann::json{{"version", 1}, {"parameters", parameters}});
}

std::expected<FrontendParameters, Error> DecodeSessionOpen(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaximumHandshakeBytes) return std::unexpected(Error::kInvalid);
    const auto document = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
    if (!document.is_object() || document.size() != 2 || !document.contains("version") || document["version"] != 1 ||
        !document.contains("parameters") || !document["parameters"].is_object() || document["parameters"].size() > kMaximumParameters)
        return std::unexpected(Error::kInvalid);
    FrontendParameters parameters{};
    for (const auto& entry : document["parameters"].items()) {
        if (!entry.value().is_string()) return std::unexpected(Error::kInvalid);
        parameters.emplace(entry.key(), entry.value().get<std::string>());
    }
    if (!ValidParameters(parameters)) return std::unexpected(Error::kInvalid);
    return parameters;
}

std::expected<Bytes, Error> EncodeSessionOpenReply(const SessionOpenReply& reply) {
    if (reply.code.empty() || reply.code.size() > 128 || (reply.accepted && reply.stream_id.empty()) || reply.stream_id.size() > 256)
        return std::unexpected(Error::kInvalid);
    if (!ValidChannels(reply.channels)) return std::unexpected(Error::kInvalid);
    return EncodeJson(nlohmann::json{
        {"version", 1}, {"accepted", reply.accepted}, {"code", reply.code}, {"stream_id", reply.stream_id}, {"channels", reply.channels}});
}

std::expected<SessionOpenReply, Error> DecodeSessionOpenReply(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > kMaximumHandshakeBytes) return std::unexpected(Error::kInvalid);
    const auto document = nlohmann::json::parse(payload.begin(), payload.end(), nullptr, false);
    if (!document.is_object() || document.size() != 5 || !document.contains("version") || document["version"] != 1 ||
        !document.contains("accepted") || !document["accepted"].is_boolean() || !document.contains("code") || !document["code"].is_string() ||
        !document.contains("stream_id") || !document["stream_id"].is_string() || !document.contains("channels") || !document["channels"].is_array() ||
        document["channels"].size() > 5)
        return std::unexpected(Error::kInvalid);
    SessionOpenReply reply{document["accepted"].get<bool>(), document["code"].get<std::string>(), document["stream_id"].get<std::string>(), {}};
    for (const auto& channel : document["channels"]) {
        if (!channel.is_number_unsigned() || channel.get<std::uint64_t>() > 5) return std::unexpected(Error::kInvalid);
        reply.channels.push_back(static_cast<ChannelKind>(channel.get<std::uint8_t>()));
    }
    if (!EncodeSessionOpenReply(reply)) return std::unexpected(Error::kInvalid);
    return reply;
}

std::expected<OpenedSession, Error> OpenFrontend(const std::shared_ptr<Connection>& connection, const FrontendParameters& parameters,
                                                 std::uint32_t timeout_ms) {
    const auto request = EncodeSessionOpen(parameters);
    if (!request || !connection) return std::unexpected(Error::kInvalid);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    const auto control = Channel::Open(connection, ChannelKind::kControl, RemainingMilliseconds(deadline));
    if (!control) return std::unexpected(control.error());
    if (const auto sent = (*control)->Send(*request, RemainingMilliseconds(deadline)); !sent) {
        connection->Close();
        return std::unexpected(sent.error());
    }
    const auto response = (*control)->Receive(RemainingMilliseconds(deadline));
    if (!response) {
        connection->Close();
        return std::unexpected(response.error());
    }
    const auto reply = DecodeSessionOpenReply(*response);
    if (!reply || (reply->accepted && reply->stream_id != parameters.at("stream_id"))) {
        connection->Close();
        return std::unexpected(Error::kInvalid);
    }
    if (!reply->accepted) {
        connection->Close();
        return OpenedSession{nullptr, *reply, {}};
    }
    SessionChannels channels{{ChannelKind::kControl, *control}};
    for (const auto kind : reply->channels) {
        if (kind == ChannelKind::kControl) continue;
        const auto channel = Channel::Open(connection, kind, RemainingMilliseconds(deadline));
        if (!channel) {
            connection->Close();
            return std::unexpected(channel.error());
        }
        channels.emplace(kind, *channel);
    }
    return OpenedSession{*control, *reply, std::move(channels)};
}
}  // namespace px::transport
