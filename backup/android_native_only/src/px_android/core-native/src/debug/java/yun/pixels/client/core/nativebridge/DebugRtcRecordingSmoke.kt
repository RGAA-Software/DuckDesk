package yun.pixels.client.core.nativebridge

import android.content.Context
import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import java.io.File
import org.webrtc.JavaI420Buffer
import org.webrtc.VideoFrame

/** Debug-build-only hardware gate used through the already installed app. */
object DebugRtcRecordingSmoke {
    fun run(context: Context): String {
        val output = File(context.cacheDir, "rtc-recording-smoke.mp4")
        output.delete()
        val runtime = WebRtcRuntime(context)
        try {
            RtcMp4Writer(output, runtime.eglContext, includeAudio = true).use { writer ->
                val startedAtNanos = System.nanoTime()
                repeat(FRAME_COUNT) { index ->
                    val frame = createFrame(index)
                    val presentationNanos = startedAtNanos + index * FRAME_DELAY_MILLIS * 1_000_000
                    try {
                        writer.writeVideo(frame, presentationNanos)
                    } finally {
                        frame.release()
                    }
                    writer.writeAudio(
                        ByteArray(AUDIO_FRAMES_PER_PACKET * AUDIO_CHANNELS * 2),
                        AUDIO_SAMPLE_RATE,
                        AUDIO_CHANNELS,
                        presentationNanos,
                    )
                    Thread.sleep(FRAME_DELAY_MILLIS)
                }
                writer.finish()
            }
            check(output.isFile && output.length() > 0L) { "recording_smoke_empty_output" }
            val extractor = MediaExtractor()
            val mimeTypes = try {
                extractor.setDataSource(output.absolutePath)
                (0 until extractor.trackCount).map { index ->
                    extractor.getTrackFormat(index).getString(MediaFormat.KEY_MIME).orEmpty()
                }.toSet()
            } finally {
                extractor.release()
            }
            check(mimeTypes == setOf(MediaFormat.MIMETYPE_VIDEO_AVC, MediaFormat.MIMETYPE_AUDIO_AAC)) {
                "recording_smoke_tracks_invalid"
            }
            val metadata = MediaMetadataRetriever()
            val durationMillis = try {
                metadata.setDataSource(output.absolutePath)
                metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
            } finally {
                metadata.release()
            }
            check(durationMillis >= MIN_OUTPUT_DURATION_MILLIS) { "recording_smoke_duration_invalid" }
            return "tracks=${mimeTypes.sorted().joinToString(",")};durationMs=$durationMillis;bytes=${output.length()}"
        } finally {
            runtime.close()
            output.delete()
        }
    }

    private fun createFrame(index: Int): VideoFrame {
        val buffer = JavaI420Buffer.allocate(VIDEO_WIDTH, VIDEO_HEIGHT)
        fill(buffer.dataY, (16 + index % 32).toByte())
        fill(buffer.dataU, 128.toByte())
        fill(buffer.dataV, 128.toByte())
        return VideoFrame(buffer, 0, index * FRAME_DELAY_MILLIS * 1_000_000)
    }

    private fun fill(buffer: java.nio.ByteBuffer, value: Byte) {
        val destination = buffer.duplicate()
        destination.clear()
        while (destination.hasRemaining()) destination.put(value)
    }
}

private const val VIDEO_WIDTH = 320
private const val VIDEO_HEIGHT = 240
private const val FRAME_COUNT = 30
private const val FRAME_DELAY_MILLIS = 17L
private const val AUDIO_SAMPLE_RATE = 48_000
private const val AUDIO_CHANNELS = 2
private const val AUDIO_FRAMES_PER_PACKET = 480
private const val MIN_OUTPUT_DURATION_MILLIS = 250L
