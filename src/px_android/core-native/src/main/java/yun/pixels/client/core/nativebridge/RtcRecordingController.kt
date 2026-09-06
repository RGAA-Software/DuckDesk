package yun.pixels.client.core.nativebridge

import android.media.MediaCodec
import android.media.MediaCodecInfo
import android.media.MediaCodecList
import android.media.MediaFormat
import android.media.MediaMuxer
import android.opengl.GLES20
import android.view.Surface
import java.io.Closeable
import java.io.File
import java.nio.ByteBuffer
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.atomic.AtomicBoolean
import java.util.concurrent.atomic.AtomicInteger
import java.util.concurrent.atomic.AtomicReference
import org.webrtc.AudioTrack
import org.webrtc.AudioTrackSink
import org.webrtc.EglBase
import org.webrtc.GlRectDrawer
import org.webrtc.VideoFrame
import org.webrtc.VideoFrameDrawer
import org.webrtc.VideoSink
import org.webrtc.VideoTrack
import yun.pixels.client.core.domain.recording.RecordingId

/** Records decoded standard-WebRTC media without screen-capture permission.
 * Video frames are rendered into a hardware AVC encoder surface and remote PCM
 * is encoded as AAC. The existing coordinator remains responsible for state,
 * notifications, staging cleanup and MediaStore publication. */
internal class RtcRecordingController(
    private val sharedEglContext: EglBase.Context,
    private val includeAudio: Boolean,
    private val onAvailabilityChanged: (Boolean) -> Unit,
    private val onStarted: (RecordingId) -> Unit,
    private val onFinished: (RecordingId, String) -> Unit,
) : Closeable {
    private val lock = Any()
    private val platformSupported = hasEncoder(MediaFormat.MIMETYPE_VIDEO_AVC) &&
        (!includeAudio || hasEncoder(MediaFormat.MIMETYPE_AUDIO_AAC))
    private var closed = false
    private var videoTrack: VideoTrack? = null
    private var audioTrack: AudioTrack? = null
    private var active: RtcRecordingSession? = null

    fun setVideoTrack(track: VideoTrack) = replaceTracks(video = track, audio = null, replaceVideo = true)

    fun setAudioTrack(track: AudioTrack) {
        if (includeAudio) replaceTracks(video = null, audio = track, replaceVideo = false)
    }

    fun start(recordingId: RecordingId, stagingDirectory: String): Boolean {
        val directory = runCatching { File(stagingDirectory).canonicalFile }.getOrNull() ?: return false
        if (!directory.isDirectory || recordingId.value.length > MAX_RECORDING_ID_CHARS) return false
        val output = File(directory, "Pixels-${recordingId.value.take(8)}.mp4")
        if (runCatching { output.canonicalFile.parentFile == directory }.getOrDefault(false).not()) return false

        return synchronized(lock) {
            val video = videoTrack
            val audio = audioTrack
            if (!platformSupported || closed || active != null || video == null || (includeAudio && audio == null)) return false
            val created = runCatching {
                RtcRecordingSession(
                    recordingId = recordingId,
                    output = output,
                    sharedEglContext = sharedEglContext,
                    videoTrack = video,
                    audioTrack = audio.takeIf { includeAudio },
                    onFinished = { finishedSession, id, error ->
                        synchronized(lock) {
                            if (active === finishedSession) active = null
                        }
                        onFinished(id, error)
                    },
                )
            }.getOrNull() ?: return false
            if (!created.attach { onStarted(recordingId) }) {
                created.abortBeforeStart()
                return false
            }
            active = created
            true
        }
    }

    fun stop(recordingId: RecordingId): Boolean {
        val session = synchronized(lock) { active?.takeIf { it.recordingId == recordingId } } ?: return false
        return session.stop("")
    }

    override fun close() {
        val session = synchronized(lock) {
            if (closed) return
            closed = true
            videoTrack = null
            audioTrack = null
            active
        }
        session?.stop("")
        onAvailabilityChanged(false)
    }

    private fun replaceTracks(video: VideoTrack?, audio: AudioTrack?, replaceVideo: Boolean) {
        val result = synchronized(lock) {
            if (closed) return
            val changed = if (replaceVideo) videoTrack !== video else audioTrack !== audio
            if (replaceVideo) videoTrack = video else audioTrack = audio
            Triple(active?.takeIf { changed }, isAvailableLocked(), changed)
        }
        if (result.third) result.first?.stop("recording_track_changed")
        onAvailabilityChanged(result.second)
    }

    private fun isAvailableLocked(): Boolean = platformSupported && !closed && videoTrack != null && (!includeAudio || audioTrack != null)
}

private class RtcRecordingSession(
    val recordingId: RecordingId,
    output: File,
    sharedEglContext: EglBase.Context,
    private val videoTrack: VideoTrack,
    private val audioTrack: AudioTrack?,
    private val onFinished: (RtcRecordingSession, RecordingId, String) -> Unit,
) : VideoSink, AudioTrackSink {
    private val submitLock = Any()
    private val accepting = AtomicBoolean(false)
    private val finalized = AtomicBoolean(false)
    private val failure = AtomicReference<String?>(null)
    private val pendingVideoFrames = AtomicInteger(0)
    private val pendingAudioPackets = AtomicInteger(0)
    private val executor: ExecutorService = Executors.newSingleThreadExecutor { task ->
        Thread(task, "pixels-rtc-recorder").apply { isDaemon = true }
    }
    private val writer = RtcMp4Writer(output, sharedEglContext, audioTrack != null)

    fun attach(onAttached: () -> Unit): Boolean = runCatching {
        videoTrack.addSink(this)
        audioTrack?.addSink(this)
        onAttached()
        accepting.set(true)
        true
    }.getOrElse {
        runCatching { videoTrack.removeSink(this) }
        runCatching { audioTrack?.removeSink(this) }
        false
    }

    fun abortBeforeStart() {
        accepting.set(false)
        runCatching { writer.close() }
        executor.shutdownNow()
    }

    override fun onFrame(frame: VideoFrame) {
        synchronized(submitLock) {
            if (!accepting.get() || pendingVideoFrames.get() >= MAX_PENDING_VIDEO_FRAMES) return
            frame.retain()
            pendingVideoFrames.incrementAndGet()
            val arrivalNanos = System.nanoTime()
            executor.execute {
                try {
                    if (failure.get() == null) writer.writeVideo(frame, arrivalNanos)
                } catch (error: Throwable) {
                    fail(error)
                } finally {
                    frame.release()
                    pendingVideoFrames.decrementAndGet()
                }
            }
        }
    }

    override fun onData(
        audioData: ByteBuffer,
        bitsPerSample: Int,
        sampleRate: Int,
        channels: Int,
        frames: Int,
        absoluteCaptureTimestampMs: Long,
    ) {
        if (bitsPerSample != PCM_BITS_PER_SAMPLE || sampleRate !in MIN_AUDIO_SAMPLE_RATE..MAX_AUDIO_SAMPLE_RATE ||
            channels !in 1..MAX_AUDIO_CHANNELS || frames <= 0
        ) {
            failCode("recording_audio_format_unsupported")
            return
        }
        val expectedBytes = frames.toLong() * channels * (bitsPerSample / 8)
        if (expectedBytes !in 1..MAX_AUDIO_PACKET_BYTES.toLong()) {
            failCode("recording_audio_packet_invalid")
            return
        }
        synchronized(submitLock) {
            if (!accepting.get() || pendingAudioPackets.get() >= MAX_PENDING_AUDIO_PACKETS) return
            val source = audioData.duplicate()
            if (source.remaining() < expectedBytes.toInt()) {
                failCode("recording_audio_packet_truncated")
                return
            }
            val bytes = ByteArray(expectedBytes.toInt())
            source.get(bytes)
            pendingAudioPackets.incrementAndGet()
            val arrivalNanos = System.nanoTime()
            executor.execute {
                try {
                    if (failure.get() == null) writer.writeAudio(bytes, sampleRate, channels, arrivalNanos)
                } catch (error: Throwable) {
                    fail(error)
                } finally {
                    pendingAudioPackets.decrementAndGet()
                }
            }
        }
    }

    fun stop(error: String): Boolean {
        synchronized(submitLock) {
            if (!accepting.compareAndSet(true, false)) return false
            if (error.isNotBlank()) failure.compareAndSet(null, error)
        }
        detachSinks()
        synchronized(submitLock) {
            executor.execute(::finalizeRecording)
            executor.shutdown()
        }
        return true
    }

    private fun fail(error: Throwable) = failCode(normalizeRecordingError(error))

    private fun failCode(error: String) {
        if (!failure.compareAndSet(null, error)) return
        runCatching { executor.execute { stop(error) } }
    }

    private fun detachSinks() {
        runCatching { videoTrack.removeSink(this) }
        runCatching { audioTrack?.removeSink(this) }
    }

    private fun finalizeRecording() {
        if (!finalized.compareAndSet(false, true)) return
        var result = failure.get().orEmpty()
        try {
            writer.finish()
        } catch (error: Throwable) {
            if (result.isBlank()) result = normalizeRecordingError(error)
        } finally {
            runCatching { writer.close() }
        }
        onFinished(this, recordingId, result)
    }
}

internal class RtcMp4Writer(
    output: File,
    private val sharedEglContext: EglBase.Context,
    private val includeAudio: Boolean,
) : Closeable {
    private enum class TrackKind { Video, Audio }

    private data class EncodedSample(val kind: TrackKind, val data: ByteArray, val presentationTimeUs: Long, val flags: Int)

    private val muxer = MediaMuxer(output.absolutePath, MediaMuxer.OutputFormat.MUXER_OUTPUT_MPEG_4)
    private val pendingSamples = ArrayList<EncodedSample>()
    private var pendingSampleBytes = 0
    private var videoCodec: MediaCodec? = null
    private var videoInputSurface: Surface? = null
    private var videoEgl: EglBase? = null
    private var videoDrawer: VideoFrameDrawer? = null
    private var glDrawer: GlRectDrawer? = null
    private var audioCodec: MediaCodec? = null
    private var audioSampleRate = 0
    private var audioChannels = 0
    private var submittedAudioFrames = 0L
    private var audioStartUs = 0L
    private var videoTrackIndex = -1
    private var audioTrackIndex = -1
    private var muxerStarted = false
    private var muxerStopped = false
    private var closed = false
    private var lastVideoPresentationUs = -1L
    private var timelineOriginNanos: Long? = null

    fun writeVideo(frame: VideoFrame, arrivalNanos: Long = System.nanoTime()) {
        check(!closed) { "recording_writer_closed" }
        val encoder = videoCodec ?: createVideoEncoder(frame).also { videoCodec = it }
        drainEncoder(encoder, TrackKind.Video, wait = false)
        val egl = checkNotNull(videoEgl) { "recording_video_egl_unavailable" }
        egl.makeCurrent()
        GLES20.glViewport(0, 0, egl.surfaceWidth(), egl.surfaceHeight())
        checkNotNull(videoDrawer).drawFrame(frame, checkNotNull(glDrawer))
        val presentationUs = presentationTimeUs(arrivalNanos).coerceAtLeast(lastVideoPresentationUs + 1)
        lastVideoPresentationUs = presentationUs
        egl.swapBuffers(presentationUs * NANOS_PER_MICROSECOND)
        drainEncoder(encoder, TrackKind.Video, wait = false)
    }

    fun writeAudio(bytes: ByteArray, sampleRate: Int, channels: Int, arrivalNanos: Long = System.nanoTime()) {
        check(!closed) { "recording_writer_closed" }
        val encoder = audioCodec ?: createAudioEncoder(sampleRate, channels).also {
            audioCodec = it
            audioStartUs = presentationTimeUs(arrivalNanos)
        }
        check(sampleRate == audioSampleRate && channels == audioChannels) { "recording_audio_format_changed" }
        drainEncoder(encoder, TrackKind.Audio, wait = false)
        var offset = 0
        while (offset < bytes.size) {
            val inputIndex = encoder.dequeueInputBuffer(0)
            if (inputIndex < 0) return
            val input = checkNotNull(encoder.getInputBuffer(inputIndex)) { "recording_audio_input_unavailable" }
            input.clear()
            val sampleFrameBytes = channels * BYTES_PER_PCM_SAMPLE
            val length = minOf(input.remaining(), bytes.size - offset).let { it - (it % sampleFrameBytes) }
            if (length <= 0) return
            input.put(bytes, offset, length)
            val frameOffset = submittedAudioFrames
            val presentationUs = audioStartUs + frameOffset * MICROS_PER_SECOND / sampleRate
            encoder.queueInputBuffer(inputIndex, 0, length, presentationUs, 0)
            submittedAudioFrames += length / sampleFrameBytes
            offset += length
        }
        drainEncoder(encoder, TrackKind.Audio, wait = false)
    }

    fun finish() {
        check(!closed) { "recording_writer_closed" }
        val video = videoCodec ?: error("recording_contains_no_video")
        video.signalEndOfInputStream()
        audioCodec?.let(::queueAudioEndOfStream)
        var videoEnded = false
        var audioEnded = !includeAudio
        var attempts = 0
        while ((!videoEnded || !audioEnded) && attempts < MAX_DRAIN_ATTEMPTS) {
            attempts += 1
            videoEnded = videoEnded || drainEncoder(video, TrackKind.Video, wait = true)
            val audio = audioCodec
            audioEnded = audioEnded || (audio != null && drainEncoder(audio, TrackKind.Audio, wait = true))
        }
        check(videoEnded) { "recording_video_drain_timeout" }
        check(audioEnded) { if (audioCodec == null) "recording_contains_no_audio" else "recording_audio_drain_timeout" }
        check(muxerStarted) { "recording_muxer_not_started" }
        muxer.stop()
        muxerStopped = true
    }

    override fun close() {
        if (closed) return
        closed = true
        runCatching { videoEgl?.detachCurrent() }
        runCatching { videoDrawer?.release() }
        runCatching { glDrawer?.release() }
        runCatching { videoEgl?.release() }
        releaseCodec(videoCodec)
        releaseCodec(audioCodec)
        runCatching { videoInputSurface?.release() }
        if (muxerStarted && !muxerStopped) runCatching { muxer.stop() }
        runCatching { muxer.release() }
    }

    private fun createVideoEncoder(frame: VideoFrame): MediaCodec {
        val width = frame.rotatedWidth.coerceAtLeast(2).let { it - it % 2 }
        val height = frame.rotatedHeight.coerceAtLeast(2).let { it - it % 2 }
        val bitRate = (width.toLong() * height * VIDEO_BITS_PER_PIXEL_SECOND)
            .coerceIn(MIN_VIDEO_BITRATE.toLong(), MAX_VIDEO_BITRATE.toLong()).toInt()
        val format = MediaFormat.createVideoFormat(MediaFormat.MIMETYPE_VIDEO_AVC, width, height).apply {
            setInteger(MediaFormat.KEY_COLOR_FORMAT, MediaCodecInfo.CodecCapabilities.COLOR_FormatSurface)
            setInteger(MediaFormat.KEY_BIT_RATE, bitRate)
            setInteger(MediaFormat.KEY_FRAME_RATE, VIDEO_FRAME_RATE)
            setInteger(MediaFormat.KEY_I_FRAME_INTERVAL, VIDEO_KEY_FRAME_INTERVAL_SECONDS)
            setInteger(MediaFormat.KEY_BITRATE_MODE, MediaCodecInfo.EncoderCapabilities.BITRATE_MODE_VBR)
        }
        val encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_VIDEO_AVC)
        try {
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            videoInputSurface = encoder.createInputSurface()
            encoder.start()
            videoEgl = EglBase.create(sharedEglContext, EglBase.CONFIG_RECORDABLE).also { egl ->
                egl.createSurface(checkNotNull(videoInputSurface))
            }
            videoDrawer = VideoFrameDrawer()
            glDrawer = GlRectDrawer()
            return encoder
        } catch (error: Throwable) {
            runCatching { encoder.release() }
            throw IllegalStateException("recording_video_encoder_unavailable", error)
        }
    }

    private fun createAudioEncoder(sampleRate: Int, channels: Int): MediaCodec {
        val format = MediaFormat.createAudioFormat(MediaFormat.MIMETYPE_AUDIO_AAC, sampleRate, channels).apply {
            setInteger(MediaFormat.KEY_AAC_PROFILE, MediaCodecInfo.CodecProfileLevel.AACObjectLC)
            setInteger(MediaFormat.KEY_BIT_RATE, if (channels == 1) MONO_AUDIO_BITRATE else STEREO_AUDIO_BITRATE)
            setInteger(MediaFormat.KEY_MAX_INPUT_SIZE, MAX_AUDIO_PACKET_BYTES)
        }
        val encoder = MediaCodec.createEncoderByType(MediaFormat.MIMETYPE_AUDIO_AAC)
        try {
            encoder.configure(format, null, null, MediaCodec.CONFIGURE_FLAG_ENCODE)
            encoder.start()
            audioSampleRate = sampleRate
            audioChannels = channels
            return encoder
        } catch (error: Throwable) {
            runCatching { encoder.release() }
            throw IllegalStateException("recording_audio_encoder_unavailable", error)
        }
    }

    private fun queueAudioEndOfStream(encoder: MediaCodec) {
        repeat(MAX_DRAIN_ATTEMPTS) {
            val inputIndex = encoder.dequeueInputBuffer(DRAIN_TIMEOUT_US)
            if (inputIndex >= 0) {
                val presentationUs = audioStartUs + submittedAudioFrames * MICROS_PER_SECOND / audioSampleRate.coerceAtLeast(1)
                encoder.queueInputBuffer(inputIndex, 0, 0, presentationUs, MediaCodec.BUFFER_FLAG_END_OF_STREAM)
                return
            }
        }
        error("recording_audio_eos_timeout")
    }

    private fun drainEncoder(encoder: MediaCodec, kind: TrackKind, wait: Boolean): Boolean {
        val info = MediaCodec.BufferInfo()
        while (true) {
            val outputIndex = encoder.dequeueOutputBuffer(info, if (wait) DRAIN_TIMEOUT_US else 0)
            when {
                outputIndex == MediaCodec.INFO_TRY_AGAIN_LATER -> return false
                outputIndex == MediaCodec.INFO_OUTPUT_FORMAT_CHANGED -> addMuxerTrack(kind, encoder.outputFormat)
                outputIndex >= 0 -> {
                    val output = checkNotNull(encoder.getOutputBuffer(outputIndex)) { "recording_encoder_output_unavailable" }
                    if (info.size > 0 && info.flags and MediaCodec.BUFFER_FLAG_CODEC_CONFIG == 0) {
                        val bytes = ByteArray(info.size)
                        output.duplicate().apply {
                            position(info.offset)
                            limit(info.offset + info.size)
                        }.get(bytes)
                        val sampleFlags = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM.inv()
                        writeOrQueue(EncodedSample(kind, bytes, info.presentationTimeUs.coerceAtLeast(0), sampleFlags))
                    }
                    val ended = info.flags and MediaCodec.BUFFER_FLAG_END_OF_STREAM != 0
                    encoder.releaseOutputBuffer(outputIndex, false)
                    if (ended) return true
                }
            }
        }
    }

    private fun addMuxerTrack(kind: TrackKind, format: MediaFormat) {
        when (kind) {
            TrackKind.Video -> {
                check(videoTrackIndex < 0) { "recording_video_format_repeated" }
                videoTrackIndex = muxer.addTrack(format)
            }
            TrackKind.Audio -> {
                check(audioTrackIndex < 0) { "recording_audio_format_repeated" }
                audioTrackIndex = muxer.addTrack(format)
            }
        }
        if (videoTrackIndex >= 0 && (!includeAudio || audioTrackIndex >= 0)) {
            muxer.start()
            muxerStarted = true
            pendingSamples.sortedBy(EncodedSample::presentationTimeUs).forEach(::writeSample)
            pendingSamples.clear()
            pendingSampleBytes = 0
        }
    }

    private fun writeOrQueue(sample: EncodedSample) {
        if (muxerStarted) {
            writeSample(sample)
            return
        }
        check(pendingSampleBytes + sample.data.size <= MAX_PENDING_ENCODED_BYTES) { "recording_muxer_start_timeout" }
        pendingSamples += sample
        pendingSampleBytes += sample.data.size
    }

    private fun writeSample(sample: EncodedSample) {
        val trackIndex = if (sample.kind == TrackKind.Video) videoTrackIndex else audioTrackIndex
        check(trackIndex >= 0) { "recording_track_unavailable" }
        val info = MediaCodec.BufferInfo().apply {
            set(0, sample.data.size, sample.presentationTimeUs, sample.flags)
        }
        muxer.writeSampleData(trackIndex, ByteBuffer.wrap(sample.data), info)
    }

    private fun presentationTimeUs(arrivalNanos: Long): Long {
        val origin = timelineOriginNanos ?: arrivalNanos.also { timelineOriginNanos = it }
        return ((arrivalNanos - origin) / NANOS_PER_MICROSECOND).coerceAtLeast(0)
    }

    private fun releaseCodec(codec: MediaCodec?) {
        if (codec == null) return
        runCatching { codec.stop() }
        runCatching { codec.release() }
    }
}

private fun normalizeRecordingError(error: Throwable): String {
    val message = generateSequence(error) { it.cause }
        .mapNotNull(Throwable::message)
        .firstOrNull { it.startsWith("recording_") }
    return message?.take(MAX_RECORDING_ERROR_CHARS) ?: "rtc_recording_failed"
}

private fun hasEncoder(mimeType: String): Boolean = runCatching {
    MediaCodecList(MediaCodecList.REGULAR_CODECS).codecInfos.any { codec ->
        codec.isEncoder && codec.supportedTypes.any { type -> type.equals(mimeType, ignoreCase = true) }
    }
}.getOrDefault(false)

private const val MAX_RECORDING_ID_CHARS = 128
private const val MAX_RECORDING_ERROR_CHARS = 256
private const val MAX_PENDING_VIDEO_FRAMES = 3
private const val MAX_PENDING_AUDIO_PACKETS = 50
private const val PCM_BITS_PER_SAMPLE = 16
private const val BYTES_PER_PCM_SAMPLE = 2
private const val MIN_AUDIO_SAMPLE_RATE = 8_000
private const val MAX_AUDIO_SAMPLE_RATE = 96_000
private const val MAX_AUDIO_CHANNELS = 2
private const val MAX_AUDIO_PACKET_BYTES = 64 * 1024
private const val MONO_AUDIO_BITRATE = 64_000
private const val STEREO_AUDIO_BITRATE = 128_000
private const val VIDEO_FRAME_RATE = 60
private const val VIDEO_KEY_FRAME_INTERVAL_SECONDS = 2
private const val VIDEO_BITS_PER_PIXEL_SECOND = 4L
private const val MIN_VIDEO_BITRATE = 2_000_000
private const val MAX_VIDEO_BITRATE = 24_000_000
private const val MAX_PENDING_ENCODED_BYTES = 32 * 1024 * 1024
private const val MAX_DRAIN_ATTEMPTS = 200
private const val DRAIN_TIMEOUT_US = 5_000L
private const val MICROS_PER_SECOND = 1_000_000L
private const val NANOS_PER_MICROSECOND = 1_000L
