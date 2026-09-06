package yun.pixels.client.core.nativebridge

import android.view.Surface

interface NativeSessionListener {
    fun onConnected(
        sessionId: String,
        monitorNames: Array<String>,
        activeMonitorName: String,
        supportsAudio: Boolean,
        supportsInput: Boolean,
        supportsFileTransfer: Boolean,
        supportsClipboard: Boolean,
        supportsVirtualDisplays: Boolean,
        ownedVirtualDisplayCount: Int,
        maximumVirtualDisplayCount: Int,
        topologyGeneration: Long,
        supportsVoiceCall: Boolean,
        voiceCallRequiresHeadset: Boolean,
    )

    fun onMonitorsChanged(sessionId: String, monitorNames: Array<String>, activeMonitorName: String)

    fun onVirtualDisplayResult(
        sessionId: String,
        requestId: String,
        accepted: Boolean,
        state: Int,
        topologyChanged: Boolean,
        topologyGeneration: Long,
        ownedDisplayCount: Int,
        errorCode: String,
        errorMessage: String,
    )

    fun onFrameSizeChanged(sessionId: String, width: Int, height: Int)

    fun onStatistics(sessionId: String, framesPerSecond: Int, latencyMillis: Int, bitrateKbps: Int)

    fun onGamepadRumble(sessionId: String, strongMotor: Int, weakMotor: Int)

    fun onClipboardText(sessionId: String, utf8Text: ByteArray)

    fun onClipboardFiles(sessionId: String, generation: String, displayNames: Array<String>, sizes: LongArray)

    fun onClipboardFilesReady(sessionId: String, generation: String, localPaths: Array<String>, utf8Error: ByteArray)

    fun onFileTransferProgress(
        sessionId: String,
        jobId: Int,
        fileNumber: Int,
        fileCount: Int,
        totalSize: Long,
        finishedSize: Long,
        transferred: Long,
        speedBytesPerSecond: Double,
        download: Boolean,
    )

    fun onFileTransferDone(sessionId: String, jobId: Int, utf8Error: ByteArray)

    fun onFileTransferOverwrite(sessionId: String, jobId: Int, fileNumber: Int, utf8Path: ByteArray, upload: Boolean, identical: Boolean)

    fun onRecordingState(sessionId: String, recordingId: String, state: Int, utf8Error: ByteArray)

    fun onVoiceCallState(
        sessionId: String,
        phase: Int,
        microphoneMuted: Boolean,
        speakerMuted: Boolean,
        requiresHeadset: Boolean,
        utf8Reason: ByteArray,
    )

    fun onDisconnected(sessionId: String, reason: Int, recoverable: Boolean)
}

internal object PixelsNativeBridge {
    init {
        System.loadLibrary("pixels_android_core")
    }

    external fun create(config: NativeSessionConfig, listener: NativeSessionListener, surface: Surface): Long

    external fun start(nativeSessionId: Long): Boolean

    external fun replaceSurface(nativeSessionId: Long, surface: Surface): Boolean

    external fun detachSurface(nativeSessionId: Long): Boolean

    external fun sendMouse(
        nativeSessionId: Long,
        action: Int,
        button: Int,
        down: Boolean,
        xRatio: Float,
        yRatio: Float,
        deltaX: Int,
        deltaY: Int,
    ): Boolean

    external fun sendKey(nativeSessionId: Long, virtualKeyCode: Int, down: Boolean): Boolean

    external fun sendText(nativeSessionId: Long, utf8Text: ByteArray): Boolean

    external fun sendClipboardText(nativeSessionId: Long, utf8Text: ByteArray): Boolean

    external fun sendClipboardFiles(
        nativeSessionId: Long,
        generation: String,
        displayNames: Array<String>,
        localPaths: Array<String>,
        sizes: LongArray,
    ): Boolean

    external fun downloadClipboardFiles(nativeSessionId: Long, generation: String, destinationDirectory: String): Boolean

    external fun startFileUpload(nativeSessionId: Long, localPath: ByteArray, remoteDirectory: ByteArray): Int

    external fun startFileDownload(nativeSessionId: Long, remotePath: ByteArray, localDirectory: ByteArray): Int

    external fun cancelFileTransfer(nativeSessionId: Long, jobId: Int): Boolean

    external fun confirmFileOverwrite(
        nativeSessionId: Long,
        jobId: Int,
        fileNumber: Int,
        overwrite: Boolean,
        offsetBytes: Long,
        applyToAll: Boolean,
    ): Boolean

    external fun sendSecureAttention(nativeSessionId: Long): Boolean

    external fun sendGamepad(
        nativeSessionId: Long,
        buttons: Int,
        leftTrigger: Int,
        rightTrigger: Int,
        leftThumbX: Int,
        leftThumbY: Int,
        rightThumbX: Int,
        rightThumbY: Int,
    ): Boolean

    external fun switchMonitor(nativeSessionId: Long, monitorName: String): Boolean

    external fun requestVirtualDisplay(
        nativeSessionId: Long,
        requestId: String,
        operation: Int,
        width: Int,
        height: Int,
        refreshHz: Int,
    ): Boolean

    external fun setAudioEnabled(nativeSessionId: Long, enabled: Boolean): Boolean

    external fun startRecording(nativeSessionId: Long, recordingId: ByteArray, stagingDirectory: ByteArray): Boolean

    external fun stopRecording(nativeSessionId: Long, recordingId: ByteArray): Boolean

    external fun startVoiceCall(nativeSessionId: Long): Boolean

    external fun stopVoiceCall(nativeSessionId: Long): Boolean

    external fun setVoiceMicrophoneMuted(nativeSessionId: Long, muted: Boolean): Boolean

    external fun setVoiceSpeakerMuted(nativeSessionId: Long, muted: Boolean): Boolean

    external fun stop(nativeSessionId: Long)
}
