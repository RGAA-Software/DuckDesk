#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace px::client::imgui {

enum class ClientStatisticsTransport : std::uint8_t { DirectUdp, DirectWebSocket, RelayWebSocket, Rdp, Iroh, RdpIroh };

struct ClientStatisticsSnapshot final {
    ClientStatisticsTransport transport{ClientStatisticsTransport::DirectUdp};
    int frameWidth{};
    int frameHeight{};
    int decodedFps{};
    int latencyMilliseconds{};
    int bitrateKbps{};
    std::optional<float> receivedFps{};
    std::optional<float> receiveMegabytesPerSecond{};
    std::optional<float> sendMegabytesPerSecond{};
    std::optional<float> decodeMilliseconds{};
    std::optional<float> receiveGapMilliseconds{};
    std::optional<int> captureFps{};
    std::optional<int> encodeFps{};
    std::optional<std::uint64_t> receivedBytes{};
    std::optional<std::uint64_t> sentBytes{};
    std::uint64_t decodedAudioFrames{};
    std::uint64_t decodedAudioBytes{};
    std::string videoFormat{};
    std::string videoColor{};
    std::string decoder{};
    std::string encoder{};
    std::string videoCapture{};
    std::string audioCapture{};
    std::string audioCodec{};
};

}  // namespace px::client::imgui
