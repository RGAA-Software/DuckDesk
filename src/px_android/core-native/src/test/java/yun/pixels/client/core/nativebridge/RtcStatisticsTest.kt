package yun.pixels.client.core.nativebridge

import java.util.LinkedHashMap
import org.junit.Assert.assertEquals
import org.junit.Test
import org.webrtc.RTCStats
import org.webrtc.RTCStatsReport

class RtcStatisticsTest {
    @Test
    fun extractsSelectedTransportAndInboundVideoMetrics() {
        val report = report(
            timestampUs = 2_000_000L,
            stats = mapOf(
                "transport" to stat("transport", mapOf("selectedCandidatePairId" to "pair-selected")),
                "pair-selected" to stat(
                    "candidate-pair",
                    mapOf("state" to "succeeded", "nominated" to true, "currentRoundTripTime" to 0.018),
                ),
                "video" to stat(
                    "inbound-rtp",
                    mapOf(
                        "kind" to "video",
                        "framesPerSecond" to 59.6,
                        "bytesReceived" to 4_000L,
                        "packetsReceived" to 190L,
                        "packetsLost" to 10L,
                    ),
                ),
                "audio" to stat("inbound-rtp", mapOf("kind" to "audio", "bytesReceived" to 9_000L)),
            ),
        ).toRtcStatisticsSample()

        assertEquals(2_000_000.0, report.timestampUs, 0.0)
        assertEquals(60, report.framesPerSecond)
        assertEquals(4_000L, report.videoBytesReceived)
        assertEquals(190L, report.videoPacketsReceived)
        assertEquals(10L, report.videoPacketsLost)
        assertEquals(0.018, report.roundTripTimeSeconds, 0.0)
    }

    @Test
    fun calculatesBoundedRatesAcrossReports() {
        val accumulator = RtcStatisticsAccumulator()
        val first = accumulator.update(sample(timestampUs = 1_000_000.0, bytesReceived = 1_000L))
        val second = accumulator.update(sample(timestampUs = 2_000_000.0, bytesReceived = 3_000L))

        assertEquals(0, first.bitrateKbps)
        assertEquals(16, second.bitrateKbps)
        assertEquals(60, second.framesPerSecond)
        assertEquals(18, second.latencyMillis)
        assertEquals(5f, second.packetLossPercent, 0.001f)
    }

    @Test
    fun counterResetDoesNotProduceNegativeBitrate() {
        val accumulator = RtcStatisticsAccumulator()
        accumulator.update(sample(timestampUs = 2_000_000.0, bytesReceived = 9_000L))

        assertEquals(0, accumulator.update(sample(timestampUs = 3_000_000.0, bytesReceived = 100L)).bitrateKbps)
    }

    @Test
    fun outOfOrderReportDoesNotReplaceTheRateBaseline() {
        val accumulator = RtcStatisticsAccumulator()
        accumulator.update(sample(timestampUs = 2_000_000.0, bytesReceived = 2_000L))
        assertEquals(0, accumulator.update(sample(timestampUs = 1_500_000.0, bytesReceived = 1_500L)).bitrateKbps)

        assertEquals(8, accumulator.update(sample(timestampUs = 3_000_000.0, bytesReceived = 3_000L)).bitrateKbps)
    }

    private fun sample(timestampUs: Double, bytesReceived: Long) = RtcStatisticsSample(
        timestampUs = timestampUs,
        framesPerSecond = 60,
        videoBytesReceived = bytesReceived,
        videoPacketsReceived = 95,
        videoPacketsLost = 5,
        roundTripTimeSeconds = 0.018,
    )

    private fun stat(type: String, members: Map<String, Any>): RTCStats = RTCStats(1_000L, type, type, members)

    private fun report(timestampUs: Long, stats: Map<String, RTCStats>): RTCStatsReport =
        RTCStatsReport(timestampUs, LinkedHashMap(stats))
}
