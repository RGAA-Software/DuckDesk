package yun.pixels.client.debug

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.util.Log
import yun.pixels.client.core.nativebridge.DebugRtcRecordingSmoke

class RtcRecordingSmokeReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != ACTION) return
        val pendingResult = goAsync()
        Thread({
            try {
                val result = DebugRtcRecordingSmoke.run(context.applicationContext)
                Log.i(TAG, "PASS $result")
            } catch (error: Throwable) {
                Log.e(TAG, "FAIL ${error.message.orEmpty().take(256)}", error)
            } finally {
                pendingResult.finish()
            }
        }, "pixels-rtc-recording-smoke").start()
    }
}

private const val ACTION = "yun.pixels.client.debug.RTC_RECORDING_SMOKE"
private const val TAG = "PixelsRtcRecordTest"
