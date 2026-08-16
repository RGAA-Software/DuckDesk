package com.pixels.yun.client.effects

import android.os.Bundle
import android.os.PowerManager
import android.os.PowerManager.WakeLock
import android.view.View
import android.view.Window
import android.view.WindowManager
import android.widget.FrameLayout
import androidx.fragment.app.FragmentActivity
import com.pixels.yun.client.App
import com.pixels.yun.client.R
import com.pixels.yun.client.Settings
import com.pixels.yun.client.impl.ThunderApp
import java.util.Timer
import java.util.TimerTask

class EffectActivity : FragmentActivity() {

    companion object {
        const val TAG = "Effect";
    }

    private lateinit var srvIp: String
    private var srvPort: Int = 0
    private lateinit var streamId: String
    private lateinit var remoteDeviceId: String
    private lateinit var appContext: com.pixels.yun.client.AppContext
    private lateinit var thunderApp: ThunderApp
    private var renderTimer: Timer = Timer()
    private lateinit var spectrumView: SpectrumView
    private var wakeLock: WakeLock? = null

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        requestWindowFeature(Window.FEATURE_NO_TITLE)
        this.window.setFlags(
            WindowManager.LayoutParams.FLAG_FULLSCREEN,
            WindowManager.LayoutParams.FLAG_FULLSCREEN
        )
        window.decorView.systemUiVisibility =
            View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or View.SYSTEM_UI_FLAG_IMMERSIVE

        setContentView(R.layout.activity_effect)
        val fragmentContainer = findViewById<FrameLayout>(R.id.id_fragment_container)
        srvIp = intent.getStringExtra("ip")!!
        srvPort = intent.getIntExtra("port", 20371)
        streamId = intent.getStringExtra("streamId")!!
        remoteDeviceId = intent.getStringExtra("remoteDeviceId")!!

        appContext = (application as App).appContext

        thunderApp = ThunderApp(srvIp, srvPort, true, false, false, streamId, remoteDeviceId)
        thunderApp.init(false, null, false, false, 0, Settings.getInstance().deviceId, streamId)
        thunderApp.start()

        spectrumView = SpectrumView(this)
        spectrumView.bind(thunderApp)
        fragmentContainer.addView(
            spectrumView,
            FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT,
                FrameLayout.LayoutParams.MATCH_PARENT
            )
        )

        renderTimer.schedule(object : TimerTask() {
            override fun run() {
                runOnUiThread {
                    spectrumView.refresh()
                }
            }
        }, 100, 16);

        val powerManager = getSystemService(POWER_SERVICE) as PowerManager
        wakeLock = powerManager.newWakeLock(PowerManager.SCREEN_BRIGHT_WAKE_LOCK, "GammaRay:WakeLock")
    }

    override fun onResume() {
        super.onResume()
        thunderApp.nativeResume()
        wakeLock?.acquire(120 * 60 * 1000L)
    }

    override fun onPause() {
        super.onPause()
        thunderApp.nativePause()
        wakeLock?.release()
    }

    override fun onDestroy() {
        super.onDestroy()
        renderTimer.cancel()
        thunderApp.nativeDestroy()
    }
}
