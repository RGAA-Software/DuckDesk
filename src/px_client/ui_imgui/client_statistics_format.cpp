#include "client_statistics_format.h"

#include <format>

namespace px::client::imgui {
namespace {

std::string TextOrUnavailable(const std::string& value) { return value.empty() ? "--" : value; }

std::string DecimalMeasurement(const std::optional<float> value, const std::string_view unit) {
    return value ? std::format("{:.2f} {}", *value, unit) : "--";
}

std::string ByteMeasurement(const std::optional<std::uint64_t> bytes) {
    return bytes ? std::format("{:.2f} MiB", static_cast<double>(*bytes) / 1'048'576.0) : "--";
}

}  // namespace

std::vector<ClientStatisticsRow> BuildClientStatisticsRows(const ClientStatisticsSnapshot& statistics, const bool english) {
    const ClientText transportLabel{statistics.transport == ClientStatisticsTransport::Rdp               ? ClientText::StatisticsRdp
                                    : statistics.transport == ClientStatisticsTransport::RelayWebSocket  ? ClientText::StatisticsRelayWebSocket
                                    : statistics.transport == ClientStatisticsTransport::DirectWebSocket ? ClientText::StatisticsDirectWebSocket
                                                                                                         : ClientText::StatisticsDirectUdp};
    const auto frameRate = [](const std::optional<int> value) { return value ? std::format("{} FPS", *value) : "--"; };
    return {{ClientText::StatisticsTransport, std::string{ClientTextValue(transportLabel, english)}},
            {ClientText::Resolution,
             statistics.frameWidth > 0 && statistics.frameHeight > 0 ? std::format("{} x {}", statistics.frameWidth, statistics.frameHeight) : "--"},
            {ClientText::StatisticsDecodedFps, frameRate(statistics.decodedFps > 0 ? std::optional{statistics.decodedFps} : std::nullopt)},
            {ClientText::StatisticsReceivedFps, DecimalMeasurement(statistics.receivedFps, "FPS")},
            {ClientText::StatisticsLatency, std::format("{} ms", statistics.latencyMilliseconds)},
            {ClientText::StatisticsBitrate, std::format("{} Kbps", statistics.bitrateKbps)},
            {ClientText::StatisticsReceiveSpeed, DecimalMeasurement(statistics.receiveMegabytesPerSecond, "MiB/s")},
            {ClientText::StatisticsSendSpeed, DecimalMeasurement(statistics.sendMegabytesPerSecond, "MiB/s")},
            {ClientText::StatisticsReceivedTotal, ByteMeasurement(statistics.receivedBytes)},
            {ClientText::StatisticsSentTotal, ByteMeasurement(statistics.sentBytes)},
            {ClientText::StatisticsDecodeTime, DecimalMeasurement(statistics.decodeMilliseconds, "ms")},
            {ClientText::StatisticsReceiveGap, DecimalMeasurement(statistics.receiveGapMilliseconds, "ms")},
            {ClientText::StatisticsVideoFormat, TextOrUnavailable(statistics.videoFormat) + " / " + TextOrUnavailable(statistics.videoColor)},
            {ClientText::StatisticsDecoder, TextOrUnavailable(statistics.decoder)},
            {ClientText::StatisticsEncoder, TextOrUnavailable(statistics.encoder)},
            {ClientText::StatisticsCaptureFps, frameRate(statistics.captureFps)},
            {ClientText::StatisticsEncodeFps, frameRate(statistics.encodeFps)},
            {ClientText::StatisticsVideoCapture, TextOrUnavailable(statistics.videoCapture)},
            {ClientText::StatisticsAudioCapture, TextOrUnavailable(statistics.audioCapture)},
            {ClientText::StatisticsAudioCodec, TextOrUnavailable(statistics.audioCodec)},
            {ClientText::StatisticsAudioFrames, std::to_string(statistics.decodedAudioFrames)},
            {ClientText::StatisticsAudioBytes, ByteMeasurement(statistics.decodedAudioBytes)}};
}

}  // namespace px::client::imgui
