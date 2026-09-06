package yun.pixels.client.remote

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.os.Binder
import android.os.IBinder
import android.net.Uri
import android.view.Surface
import androidx.core.app.NotificationCompat
import androidx.compose.runtime.Stable
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.flow.MutableStateFlow
import yun.pixels.client.MainActivity
import yun.pixels.client.PixelsApplication
import yun.pixels.client.R
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionSnapshot
import yun.pixels.client.core.domain.session.RemoteSessionStatus
import yun.pixels.client.core.nativebridge.NativeRemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteSessionWorkflow
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteKey
import yun.pixels.client.core.domain.session.RemoteGamepadState
import yun.pixels.client.core.domain.session.RemoteMouseButton
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation
import yun.pixels.client.core.domain.transfer.FileTransferTask
import yun.pixels.client.core.domain.transfer.FileTransferState
import yun.pixels.client.core.domain.recording.RecordingState
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collectLatest

class RemoteSessionService : Service() {
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private lateinit var transport: NativeRemoteSessionTransport
    private lateinit var workflow: RemoteSessionWorkflow
    private lateinit var fileTransfers: AndroidFileTransferCoordinator
    private lateinit var recordings: AndroidRecordingCoordinator
    private val localBinder = LocalBinder()
    private var preparedRequest: RemoteSessionRequest? = null
    private var foregroundStarted = false
    private var foregroundDeviceName = "Pixels"
    private lateinit var audioManager: AudioManager
    private var audioFocusRequest: AudioFocusRequest? = null
    private var hasAudioFocus = false
    private var userWantsAudio = true
    private val mutableAudioEnabled = MutableStateFlow(false)
    private val heldKeys = mutableSetOf<RemoteKey>()
    private val heldMouseButtons = mutableSetOf<RemoteMouseButton>()
    private var gamepadActive = false
    private val audioFocusListener = AudioManager.OnAudioFocusChangeListener { focusChange ->
        hasAudioFocus = focusChange == AudioManager.AUDIOFOCUS_GAIN
        if (focusChange == AudioManager.AUDIOFOCUS_LOSS) {
            audioFocusRequest?.let(audioManager::abandonAudioFocusRequest)
            audioFocusRequest = null
        }
        mutableAudioEnabled.value = userWantsAudio && hasAudioFocus
        currentRequest()?.let { request ->
            serviceScope.launch { transport.setAudioEnabled(request.id, mutableAudioEnabled.value) }
        }
    }

    override fun onCreate() {
        super.onCreate()
        val graph = (application as PixelsApplication).graph
        audioManager = getSystemService(AudioManager::class.java)
        transport = NativeRemoteSessionTransport(graph.installationIdentity, serviceScope)
        workflow = RemoteSessionWorkflow(transport, serviceScope)
        fileTransfers = AndroidFileTransferCoordinator(this, transport, serviceScope)
        recordings = AndroidRecordingCoordinator(this, transport, serviceScope)
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
        serviceScope.launch {
            fileTransfers.tasks.collectLatest { tasks ->
                if (foregroundStarted) {
                    updateNotification(tasks, recordings.state.value)
                }
            }
        }
        serviceScope.launch {
            recordings.state.collectLatest { state ->
                if (foregroundStarted) updateNotification(fileTransfers.tasks.value, state)
            }
        }
    }

    override fun onBind(intent: Intent): IBinder = localBinder

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        if (intent?.action == ACTION_STOP) stopSession()
        return START_NOT_STICKY
    }

    override fun onDestroy() {
        runBlocking {
            currentRequest()?.let {
                releaseAllInputs(it.id)
                recordings.sessionEnded(it.id)
                fileTransfers.sessionEnded(it.id)
            }
            workflow.close()
        }
        recordings.close()
        abandonAudioFocus()
        serviceScope.cancel()
        super.onDestroy()
    }

    private fun prepare(request: RemoteSessionRequest) {
        preparedRequest = request
        userWantsAudio = request.enableAudio
        if (request.enableAudio) requestAudioFocus() else abandonAudioFocus()
        mutableAudioEnabled.value = request.enableAudio && hasAudioFocus
        startService(Intent(this, RemoteSessionService::class.java))
        startForegroundSession(request.target.displayName, request.enableAudio)
    }

    private fun attachSurface(surface: Surface) {
        val request = preparedRequest ?: currentRequest() ?: return
        serviceScope.launch {
            transport.attachSurface(request.id, surface)
            workflow.start(request)
            transport.setAudioEnabled(request.id, mutableAudioEnabled.value)
        }
    }

    private fun detachSurface(surface: Surface) {
        val request = currentRequest() ?: preparedRequest ?: return
        serviceScope.launch {
            releaseAllInputs(request.id)
            transport.detachSurface(request.id, surface)
        }
    }

    private fun sendInput(command: InputCommand) {
        val request = currentRequest() ?: return
        serviceScope.launch {
            if (transport.sendInput(request.id, command)) updateHeldInputs(command)
        }
    }

    private fun sendText(text: String) {
        if (currentRequest() == null) return
        val command = runCatching { InputCommand.Text(text) }.getOrNull() ?: return
        sendInput(command)
    }

    private fun sendClipboardText(text: String) {
        val request = currentRequest() ?: return
        serviceScope.launch { transport.sendClipboardText(request.id, text) }
    }

    private fun setAudioEnabled(enabled: Boolean) {
        userWantsAudio = enabled
        if (enabled) requestAudioFocus() else abandonAudioFocus()
        mutableAudioEnabled.value = enabled && hasAudioFocus
        currentRequest()?.let { request ->
            serviceScope.launch { transport.setAudioEnabled(request.id, mutableAudioEnabled.value) }
        }
    }

    private fun stopSession() {
        preparedRequest = null
        serviceScope.launch {
            val sessionId = currentRequest()?.id
            releaseAllInputs(sessionId)
            sessionId?.let { recordings.sessionEnded(it) }
            sessionId?.let(fileTransfers::sessionEnded)
            workflow.stop()
            abandonAudioFocus()
            mutableAudioEnabled.value = false
            if (foregroundStarted) {
                stopForeground(STOP_FOREGROUND_REMOVE)
                foregroundStarted = false
            }
            stopSelf()
        }
    }

    private fun updateHeldInputs(command: InputCommand) {
        when (command) {
            is InputCommand.Key -> if (command.down) heldKeys += command.key else heldKeys -= command.key
            is InputCommand.MouseButton -> if (command.down) heldMouseButtons += command.button else heldMouseButtons -= command.button
            is InputCommand.Gamepad -> gamepadActive = command.state != RemoteGamepadState()
            else -> Unit
        }
    }

    private suspend fun releaseAllInputs(sessionId: RemoteSessionId?) {
        if (sessionId == null) return
        heldMouseButtons.toList().forEach { button -> transport.sendInput(sessionId, InputCommand.MouseButton(button, false)) }
        heldKeys.toList().forEach { key -> transport.sendInput(sessionId, InputCommand.Key(key, false)) }
        if (gamepadActive) transport.sendInput(sessionId, InputCommand.Gamepad(RemoteGamepadState()))
        heldMouseButtons.clear()
        heldKeys.clear()
        gamepadActive = false
    }

    private fun switchMonitor(monitorName: String) {
        val request = currentRequest() ?: return
        serviceScope.launch { transport.switchMonitor(request.id, monitorName) }
    }

    private fun requestVirtualDisplay(requestId: String, operation: RemoteVirtualDisplayOperation) {
        val request = currentRequest() ?: return
        serviceScope.launch { transport.requestVirtualDisplay(request.id, requestId, operation) }
    }

    private fun startUpload(source: Uri, remoteDirectory: String) {
        val connected = workflow.snapshot.value.status as? RemoteSessionStatus.Connected ?: return
        if (!connected.capabilities.supportsFileTransfer || remoteDirectory.isBlank()) return
        fileTransfers.upload(connected.request.id, source, remoteDirectory)
    }

    private fun startDownload(remotePath: String, destination: Uri) {
        val connected = workflow.snapshot.value.status as? RemoteSessionStatus.Connected ?: return
        if (!connected.capabilities.supportsFileTransfer || remotePath.isBlank()) return
        fileTransfers.download(connected.request.id, remotePath, destination)
    }

    private fun startRecording() {
        val connected = workflow.snapshot.value.status as? RemoteSessionStatus.Connected ?: return
        recordings.start(connected.request.id)
    }

    private fun currentRequest(): RemoteSessionRequest? = when (val status = workflow.snapshot.value.status) {
        RemoteSessionStatus.Idle -> null
        is RemoteSessionStatus.Starting -> status.request
        is RemoteSessionStatus.Connected -> status.request
        is RemoteSessionStatus.Reconnecting -> status.request
        is RemoteSessionStatus.Stopping -> status.request
        is RemoteSessionStatus.Failed -> status.request
    }

    private fun startForegroundSession(deviceName: String, withAudio: Boolean) {
        foregroundDeviceName = deviceName
        val notification = createNotification(deviceName)
        val serviceTypes = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or
            if (withAudio) ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK else 0
        startForeground(NOTIFICATION_ID, notification, serviceTypes)
        foregroundStarted = true
    }

    private fun requestAudioFocus() {
        if (audioFocusRequest != null) return
        val attributes = AudioAttributes.Builder()
            .setUsage(AudioAttributes.USAGE_GAME)
            .setContentType(AudioAttributes.CONTENT_TYPE_MOVIE)
            .build()
        val request = AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN)
            .setAudioAttributes(attributes)
            .setAcceptsDelayedFocusGain(true)
            .setOnAudioFocusChangeListener(audioFocusListener)
            .build()
        audioFocusRequest = request
        hasAudioFocus = audioManager.requestAudioFocus(request) == AudioManager.AUDIOFOCUS_REQUEST_GRANTED
    }

    private fun abandonAudioFocus() {
        audioFocusRequest?.let(audioManager::abandonAudioFocusRequest)
        audioFocusRequest = null
        hasAudioFocus = false
    }

    private fun updateNotification(tasks: List<FileTransferTask>, recordingState: RecordingState) {
        getSystemService(NotificationManager::class.java).notify(
            NOTIFICATION_ID,
            createNotification(foregroundDeviceName, tasks, recordingState),
        )
    }

    private fun createNotification(
        deviceName: String,
        tasks: List<FileTransferTask> = fileTransfers.tasks.value,
        recordingState: RecordingState = recordings.state.value,
    ): Notification {
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
        val activeTransfer = tasks.firstOrNull { it.state in ACTIVE_TRANSFER_STATES }
        val builder = NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_pixels_notification)
            .setContentTitle(getString(R.string.remote_notification_title))
            .setContentText(
                when {
                    recordingState is RecordingState.Recording || recordingState is RecordingState.Stopping ||
                        recordingState is RecordingState.Publishing -> getString(R.string.remote_notification_recording)
                    activeTransfer != null -> getString(R.string.remote_notification_transfer, activeTransfer.name)
                    else -> getString(R.string.remote_notification_text, deviceName)
                },
            )
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .addAction(0, getString(R.string.remote_notification_stop), stopIntent)
        if (activeTransfer != null) {
            val total = activeTransfer.totalBytes
            builder.setProgress(
                100,
                if (total > 0) ((activeTransfer.completedBytes.coerceIn(0, total).toDouble() / total.toDouble()) * 100).toInt() else 0,
                total <= 0,
            )
        }
        return builder.build()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(CHANNEL_ID, getString(R.string.remote_notification_channel), NotificationManager.IMPORTANCE_LOW)
        getSystemService(NotificationManager::class.java).createNotificationChannel(channel)
    }

    @Stable
    inner class LocalBinder : Binder() {
        val snapshot: StateFlow<RemoteSessionSnapshot>
            get() = workflow.snapshot

        val audioEnabled: StateFlow<Boolean>
            get() = mutableAudioEnabled.asStateFlow()

        val fileTransferTasks: StateFlow<List<FileTransferTask>>
            get() = fileTransfers.tasks

        val recordingState: StateFlow<RecordingState>
            get() = recordings.state

        fun prepare(request: RemoteSessionRequest) = this@RemoteSessionService.prepare(request)

        fun attachSurface(surface: Surface) = this@RemoteSessionService.attachSurface(surface)

        fun detachSurface(surface: Surface) = this@RemoteSessionService.detachSurface(surface)

        fun sendInput(command: InputCommand) = this@RemoteSessionService.sendInput(command)

        fun switchMonitor(monitorName: String) = this@RemoteSessionService.switchMonitor(monitorName)

        fun requestVirtualDisplay(requestId: String, operation: RemoteVirtualDisplayOperation) =
            this@RemoteSessionService.requestVirtualDisplay(requestId, operation)

        fun sendText(text: String) = this@RemoteSessionService.sendText(text)

        fun sendClipboardText(text: String) = this@RemoteSessionService.sendClipboardText(text)

        fun setAudioEnabled(enabled: Boolean) = this@RemoteSessionService.setAudioEnabled(enabled)

        fun startUpload(source: Uri, remoteDirectory: String) = this@RemoteSessionService.startUpload(source, remoteDirectory)

        fun startDownload(remotePath: String, destination: Uri) = this@RemoteSessionService.startDownload(remotePath, destination)

        fun cancelTransfer(taskId: String) = fileTransfers.cancel(taskId)

        fun retryTransfer(taskId: String) = fileTransfers.retry(taskId)

        fun resolveTransferOverwrite(taskId: String, overwrite: Boolean, applyToAll: Boolean) =
            fileTransfers.resolveOverwrite(taskId, overwrite, applyToAll)

        fun clearFinishedTransfers() = fileTransfers.clearFinished()

        fun startRecording() = this@RemoteSessionService.startRecording()

        fun stopRecording() = recordings.stop()

        fun stopSession() = this@RemoteSessionService.stopSession()
    }

    companion object {
        private const val CHANNEL_ID = "pixels_remote_session"
        private const val NOTIFICATION_ID = 1201
        private const val ACTION_STOP = "yun.pixels.client.action.STOP_REMOTE_SESSION"
        private val ACTIVE_TRANSFER_STATES = setOf(
            FileTransferState.Preparing,
            FileTransferState.Queued,
            FileTransferState.Running,
            FileTransferState.AwaitingOverwrite,
        )
    }
}
