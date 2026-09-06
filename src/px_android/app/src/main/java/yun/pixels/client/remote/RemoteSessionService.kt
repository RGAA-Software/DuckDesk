package yun.pixels.client.remote

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Binder
import android.os.IBinder
import android.view.Surface
import androidx.core.app.NotificationCompat
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import yun.pixels.client.MainActivity
import yun.pixels.client.PixelsApplication
import yun.pixels.client.R
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionSnapshot
import yun.pixels.client.core.domain.session.RemoteSessionStatus
import yun.pixels.client.core.nativebridge.NativeRemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteSessionWorkflow
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.PointerAction
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.collectLatest

class RemoteSessionService : Service() {
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private lateinit var transport: NativeRemoteSessionTransport
    private lateinit var workflow: RemoteSessionWorkflow
    private val localBinder = LocalBinder()
    private var preparedRequest: RemoteSessionRequest? = null
    private var foregroundStarted = false

    override fun onCreate() {
        super.onCreate()
        val graph = (application as PixelsApplication).graph
        transport = NativeRemoteSessionTransport(graph.installationIdentity, serviceScope)
        workflow = RemoteSessionWorkflow(transport, serviceScope)
        createNotificationChannel()
        serviceScope.launch {
            workflow.snapshot.collectLatest { snapshot ->
                if (snapshot.status is RemoteSessionStatus.Failed && foregroundStarted) {
                    stopForeground(STOP_FOREGROUND_REMOVE)
                    foregroundStarted = false
                    stopSelf()
                }
            }
        }
    }

    override fun onBind(intent: Intent): IBinder = localBinder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) stopSession()
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        runBlocking { workflow.close() }
        serviceScope.cancel()
        super.onDestroy()
    }

    private fun prepare(request: RemoteSessionRequest) {
        preparedRequest = request
        startService(Intent(this, RemoteSessionService::class.java))
        startForegroundSession(request.target.displayName)
    }

    private fun attachSurface(surface: Surface) {
        val request = preparedRequest ?: currentRequest() ?: return
        serviceScope.launch {
            transport.attachSurface(request.id, surface)
            workflow.start(request)
        }
    }

    private fun detachSurface() {
        val request = currentRequest() ?: preparedRequest ?: return
        serviceScope.launch { transport.detachSurface(request.id) }
    }

    private fun sendPointer(action: PointerAction, xRatio: Float, yRatio: Float) {
        val request = currentRequest() ?: return
        serviceScope.launch {
            transport.sendInput(request.id, InputCommand.Pointer(action, xRatio.coerceIn(0f, 1f), yRatio.coerceIn(0f, 1f)))
        }
    }

    private fun stopSession() {
        preparedRequest = null
        serviceScope.launch {
            workflow.stop()
            if (foregroundStarted) {
                stopForeground(STOP_FOREGROUND_REMOVE)
                foregroundStarted = false
            }
            stopSelf()
        }
    }

    private fun currentRequest(): RemoteSessionRequest? = when (val status = workflow.snapshot.value.status) {
        RemoteSessionStatus.Idle -> null
        is RemoteSessionStatus.Starting -> status.request
        is RemoteSessionStatus.Connected -> status.request
        is RemoteSessionStatus.Reconnecting -> status.request
        is RemoteSessionStatus.Stopping -> status.request
        is RemoteSessionStatus.Failed -> status.request
    }

    private fun startForegroundSession(deviceName: String) {
        val notification = createNotification(deviceName)
        startForeground(NOTIFICATION_ID, notification, ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
        foregroundStarted = true
    }

    private fun createNotification(deviceName: String): Notification {
        val contentIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        val stopIntent = PendingIntent.getService(
            this,
            1,
            Intent(this, RemoteSessionService::class.java).setAction(ACTION_STOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT,
        )
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_pixels_notification)
            .setContentTitle(getString(R.string.remote_notification_title))
            .setContentText(getString(R.string.remote_notification_text, deviceName))
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .addAction(0, getString(R.string.remote_notification_stop), stopIntent)
            .build()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(CHANNEL_ID, getString(R.string.remote_notification_channel), NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    inner class LocalBinder : Binder() {
        val snapshot: StateFlow<RemoteSessionSnapshot>
            get() = workflow.snapshot

        fun prepare(request: RemoteSessionRequest) = this@RemoteSessionService.prepare(request)

        fun attachSurface(surface: Surface) = this@RemoteSessionService.attachSurface(surface)

        fun detachSurface() = this@RemoteSessionService.detachSurface()

        fun sendPointer(action: PointerAction, xRatio: Float, yRatio: Float) =
            this@RemoteSessionService.sendPointer(action, xRatio, yRatio)

        fun stopSession() = this@RemoteSessionService.stopSession()
    }

    companion object {
        private const val CHANNEL_ID = "pixels_remote_session"
        private const val NOTIFICATION_ID = 1201
        private const val ACTION_STOP = "yun.pixels.client.action.STOP_REMOTE_SESSION"
    }
}
