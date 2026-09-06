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

    external fun stop(nativeSessionId: Long)
}
