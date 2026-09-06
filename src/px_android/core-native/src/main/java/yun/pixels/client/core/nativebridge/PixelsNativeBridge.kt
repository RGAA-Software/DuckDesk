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

    fun onDisconnected(sessionId: String, reason: Int, recoverable: Boolean)
}

internal object PixelsNativeBridge {
    init {
        System.loadLibrary("pixels_android_core")
    }

    external fun create(config: NativeSessionConfig, listener: NativeSessionListener, surface: Surface): Long

    external fun start(nativeSessionId: Long): Boolean

    external fun replaceSurface(nativeSessionId: Long, surface: Surface): Boolean

    external fun sendPointer(nativeSessionId: Long, action: Int, xRatio: Float, yRatio: Float): Boolean

    external fun stop(nativeSessionId: Long)
}
