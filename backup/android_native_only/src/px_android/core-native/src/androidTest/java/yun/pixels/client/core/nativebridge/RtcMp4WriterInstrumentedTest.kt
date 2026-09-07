package yun.pixels.client.core.nativebridge

import android.media.MediaExtractor
import android.media.MediaFormat
import android.media.MediaMetadataRetriever
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.webrtc.EglBase
import org.webrtc.JavaI420Buffer
import org.webrtc.VideoFrame

@RunWith(AndroidJUnit4::class)
class RtcMp4WriterInstrumentedTest {
    @Test
    fun hardwareEncodesVideoAndRemotePcmIntoPlayableMp4() {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val output = File(context.cacheDir, "rtc-recording-smoke.mp4")
        output.delete()
        val egl = EglBase.create()
        try {
            RtcMp4Writer(output, egl.eglBaseContext, includeAudio = true).use { writer ->
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

            assertTrue(output.isFile)
            assertTrue(output.length() > 0L)
            val extractor = MediaExtractor()
            try {
                extractor.setDataSource(output.absolutePath)
                val mimeTypes = (0 until extractor.trackCount).map { index ->
                    extractor.getTrackFormat(index).getString(MediaFormat.KEY_MIME).orEmpty()
                }.toSet()
                assertEquals(setOf(MediaFormat.MIMETYPE_VIDEO_AVC, MediaFormat.MIMETYPE_AUDIO_AAC), mimeTypes)
            } finally {
                extractor.release()
            }
            val metadata = MediaMetadataRetriever()
            try {
                metadata.setDataSource(output.absolutePath)
                val durationMillis = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION)?.toLongOrNull() ?: 0L
                assertTrue(durationMillis >= MIN_OUTPUT_DURATION_MILLIS)
            } finally {
                metadata.release()
            }
        } finally {
            egl.release()
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
