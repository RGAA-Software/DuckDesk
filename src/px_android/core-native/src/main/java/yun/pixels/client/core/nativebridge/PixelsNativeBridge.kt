package yun.pixels.client.core.nativebridge

import android.view.Surface

interface NativeSessionListener {
    fun onConnected(
        sessionId: String,
        activeMonitorName: String,
        supportsAudio: Boolean,
        supportsInput: Boolean,
        supportsFileTransfer: Boolean,
        supportsClipboard: Boolean,
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

    external fun setAudioEnabled(nativeSessionId: Long, enabled: Boolean): Boolean

    external fun stop(nativeSessionId: Long)
}
