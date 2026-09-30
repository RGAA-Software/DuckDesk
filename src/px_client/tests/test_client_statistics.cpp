#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <string>
#include <utility>

#include "client_statistics_format.h"

namespace px::client::imgui {
namespace {

std::string RowValue(const std::vector<ClientStatisticsRow>& rows, const ClientText label) {
    const auto found = std::ranges::find(rows, label, &ClientStatisticsRow::label);
    return found == rows.end() ? std::string{} : found->value;
}

TEST(ClientStatistics, FormatsExistingMeasurementsWithCorrectUnits) {
    ClientStatisticsSnapshot statistics{.frameWidth = 1920,
                                        .frameHeight = 1080,
                                        .decodedFps = 60,
                                        .latencyMilliseconds = 25,
                                        .bitrateKbps = 8000,
                                        .receivedFps = 60.0F,
                                        .receiveMegabytesPerSecond = 1.0F,
                                        .sendMegabytesPerSecond = 0.125F,
                                        .decodeMilliseconds = 2.5F,
                                        .receiveGapMilliseconds = 16.67F,
                                        .captureFps = 60,
                                        .encodeFps = 59,
                                        .receivedBytes = 2'097'152U,
                                        .sentBytes = 1'048'576U,
                                        .decodedAudioFrames = 120U,
                                        .decodedAudioBytes = 524'288U,
                                        .videoFormat = "H264",
                                        .videoColor = "4:2:0",
                                        .decoder = "D3D11VA",
                                        .encoder = "NVENC",
                                        .videoCapture = "DXGI",
                                        .audioCapture = "WASAPI",
                                        .audioCodec = "OPUS"};
    const auto rows = BuildClientStatisticsRows(statistics, true);
    EXPECT_EQ(rows.size(), 22U);
    EXPECT_EQ(RowValue(rows, ClientText::Resolution), "1920 x 1080");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsDecodedFps), "60 FPS");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsLatency), "25 ms");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsBitrate), "8000 Kbps");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsReceiveSpeed), "1.00 MiB/s");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsReceivedTotal), "2.00 MiB");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsDecodeTime), "2.50 ms");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsVideoFormat), "H264 / 4:2:0");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsDecoder), "D3D11VA");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsEncoder), "NVENC");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsAudioBytes), "0.50 MiB");
}

TEST(ClientStatistics, MissingMeasurementsAreNotFabricatedAsZero) {
    const auto rows = BuildClientStatisticsRows({}, true);
    for (const ClientText label :
         {ClientText::Resolution, ClientText::StatisticsReceivedFps, ClientText::StatisticsDecodeTime, ClientText::StatisticsReceiveSpeed,
          ClientText::StatisticsSentTotal, ClientText::StatisticsCaptureFps, ClientText::StatisticsEncoder, ClientText::StatisticsAudioCodec}) {
        EXPECT_EQ(RowValue(rows, label), "--");
    }
}

TEST(ClientStatistics, MeasuredZeroRemainsDifferentFromUnavailable) {
    const auto rows = BuildClientStatisticsRows({.receivedFps = 0.0F, .receiveMegabytesPerSecond = 0.0F, .captureFps = 0, .receivedBytes = 0}, true);
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsReceivedFps), "0.00 FPS");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsReceiveSpeed), "0.00 MiB/s");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsCaptureFps), "0 FPS");
    EXPECT_EQ(RowValue(rows, ClientText::StatisticsReceivedTotal), "0.00 MiB");
}

TEST(ClientStatistics, TransportLabelsFollowActualSnapshotAndLanguage) {
    for (const auto [transport, label] : std::array{std::pair{ClientStatisticsTransport::DirectUdp, ClientText::StatisticsDirectUdp},
                                                    std::pair{ClientStatisticsTransport::DirectWebSocket, ClientText::StatisticsDirectWebSocket},
                                                    std::pair{ClientStatisticsTransport::RelayWebSocket, ClientText::StatisticsRelayWebSocket},
                                                    std::pair{ClientStatisticsTransport::Rdp, ClientText::StatisticsRdp}}) {
        for (const bool english : {false, true}) {
            const auto rows = BuildClientStatisticsRows({.transport = transport}, english);
            EXPECT_EQ(RowValue(rows, ClientText::StatisticsTransport), ClientTextValue(label, english));
        }
    }
}

TEST(ClientStatistics, ClientCatalogsHaveCompleteBilingualCoverage) {
    for (std::uint8_t textIndex{}; textIndex < std::to_underlying(ClientText::Count); ++textIndex) {
        const auto textId = static_cast<ClientText>(textIndex);
        EXPECT_FALSE(ClientTextValue(textId, true).empty()) << static_cast<int>(textIndex);
        EXPECT_FALSE(ClientTextValue(textId, false).empty()) << static_cast<int>(textIndex);
    }
    EXPECT_EQ(ClientTextValue(ClientText::SecureAttention, false), "Ctrl+Alt+Del");
}

}  // namespace
}  // namespace px::client::imgui
