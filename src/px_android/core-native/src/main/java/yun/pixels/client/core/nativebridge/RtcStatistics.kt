package yun.pixels.client.core.nativebridge

import kotlin.math.roundToInt
import org.webrtc.RTCStatsReport
import yun.pixels.client.core.domain.session.RemoteSessionStatistics

internal data class RtcStatisticsSample(
    val timestampUs: Double,
    val framesPerSecond: Int,
    val videoBytesReceived: Long,
    val videoPacketsReceived: Long,
    val videoPacketsLost: Long,
    val roundTripTimeSeconds: Double,
)

internal class RtcStatisticsAccumulator {
    private var previousTimestampUs: Double? = null
    private var previousVideoBytesReceived: Long? = null

    fun update(sample: RtcStatisticsSample): RemoteSessionStatistics {
        val previousTimestamp = previousTimestampUs
        val previousBytes = previousVideoBytesReceived
        val elapsedUs = if (previousTimestamp == null) 0.0 else sample.timestampUs - previousTimestamp
        val receivedDelta = if (previousBytes == null) 0L else sample.videoBytesReceived - previousBytes
        val bitrateKbps = if (elapsedUs > 0.0 && receivedDelta >= 0L) {
            (receivedDelta.toDouble() * BITS_PER_KILOBIT_MICROSECOND / elapsedUs)
                .roundToInt()
                .coerceIn(0, MAX_RTC_BITRATE_KBPS)
        } else {
            0
        }
        if (previousTimestamp == null || sample.timestampUs > previousTimestamp) {
            previousTimestampUs = sample.timestampUs
            previousVideoBytesReceived = sample.videoBytesReceived
        }

        val packetTotal = sample.videoPacketsReceived.toDouble() + sample.videoPacketsLost.toDouble()
        val packetLossPercent = if (packetTotal > 0.0) {
            (sample.videoPacketsLost.toDouble() * 100.0 / packetTotal)
                .toFloat()
                .coerceIn(0f, 100f)
        } else {
            0f
        }
        return RemoteSessionStatistics(
            framesPerSecond = sample.framesPerSecond.coerceIn(0, MAX_RTC_FRAMES_PER_SECOND),
            latencyMillis = (sample.roundTripTimeSeconds * 1_000.0)
                .roundToInt()
                .coerceIn(0, MAX_RTC_LATENCY_MILLIS),
            bitrateKbps = bitrateKbps,
            packetLossPercent = packetLossPercent,
        )
    }
}

internal fun RTCStatsReport.toRtcStatisticsSample(): RtcStatisticsSample {
    val inboundVideo = statsMap.values.filter { stat ->
        stat.type == "inbound-rtp" && (stat.members["kind"] == "video" || stat.members["mediaType"] == "video")
    }
    val selectedPairId = statsMap.values.firstOrNull { stat -> stat.type == "transport" }
        ?.members
        ?.get("selectedCandidatePairId") as? String
    val selectedPair = selectedPairId?.let(statsMap::get) ?: statsMap.values.firstOrNull { stat ->
        stat.type == "candidate-pair" && stat.members["state"] == "succeeded" && stat.members["nominated"] == true
    }
    return RtcStatisticsSample(
        timestampUs = timestampUs.takeIf(Double::isFinite)?.coerceAtLeast(0.0) ?: 0.0,
        framesPerSecond = inboundVideo.maxOfOrNull { stat -> stat.members["framesPerSecond"].asBoundedInt() } ?: 0,
        videoBytesReceived = inboundVideo.sumBounded("bytesReceived"),
        videoPacketsReceived = inboundVideo.sumBounded("packetsReceived"),
        videoPacketsLost = inboundVideo.sumBounded("packetsLost"),
        roundTripTimeSeconds = selectedPair?.members?.get("currentRoundTripTime")
            .asBoundedDouble(MAX_RTC_ROUND_TRIP_SECONDS),
    )
}

private fun List<org.webrtc.RTCStats>.sumBounded(member: String): Long = fold(0L) { total, stat ->
    val value = stat.members[member].asBoundedLong()
    if (Long.MAX_VALUE - total < value) Long.MAX_VALUE else total + value
}

private fun Any?.asBoundedInt(): Int = (this as? Number)
    ?.toDouble()
    ?.takeIf(Double::isFinite)
    ?.coerceIn(0.0, Int.MAX_VALUE.toDouble())
    ?.roundToInt()
    ?: 0

private fun Any?.asBoundedLong(): Long = (this as? Number)
    ?.toDouble()
    ?.takeIf(Double::isFinite)
    ?.coerceIn(0.0, Long.MAX_VALUE.toDouble())
    ?.toLong()
    ?: 0L

private fun Any?.asBoundedDouble(maximum: Double): Double = (this as? Number)
    ?.toDouble()
    ?.takeIf(Double::isFinite)
    ?.coerceIn(0.0, maximum)
    ?: 0.0

private const val BITS_PER_KILOBIT_MICROSECOND = 8_000.0
private const val MAX_RTC_FRAMES_PER_SECOND = 240
private const val MAX_RTC_LATENCY_MILLIS = 60_000
private const val MAX_RTC_BITRATE_KBPS = 1_000_000
private const val MAX_RTC_ROUND_TRIP_SECONDS = 60.0
