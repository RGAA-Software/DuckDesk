package yun.pixels.client.remote

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.media.AudioAttributes
import android.media.AudioFocusRequest
import android.media.AudioManager
import android.media.AudioDeviceInfo
import android.os.Build
import android.os.Binder
import android.os.IBinder
import android.net.Uri
import android.view.Surface
import androidx.core.app.NotificationCompat
import androidx.core.content.ContextCompat
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
import yun.pixels.client.core.domain.session.ClipboardDownloadState
import yun.pixels.client.core.domain.session.RemoteClipboardFiles
import yun.pixels.client.core.nativebridge.NativeRemoteSessionTransport
import yun.pixels.client.core.domain.session.RemoteSessionWorkflow
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteKey
import yun.pixels.client.core.domain.session.RemoteGamepadState
import yun.pixels.client.core.domain.session.RemoteMouseButton
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.session.RemoteTransportEvent
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation
import yun.pixels.client.core.domain.transfer.FileTransferTask
import yun.pixels.client.core.domain.transfer.FileTransferState
import yun.pixels.client.core.domain.transfer.RemoteDirectoryState
import yun.pixels.client.core.domain.recording.RecordingState
import yun.pixels.client.core.domain.voice.VoiceCallPhase
import yun.pixels.client.core.domain.voice.VoiceCallState
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.collect
import kotlinx.coroutines.flow.collectLatest

class RemoteSessionService : Service() {
    private val serviceScope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private lateinit var transport: NativeRemoteSessionTransport
    private lateinit var workflow: RemoteSessionWorkflow
    private lateinit var fileTransfers: AndroidFileTransferCoordinator
    private lateinit var clipboard: AndroidClipboardCoordinator
    private lateinit var recordings: AndroidRecordingCoordinator
    private lateinit var gamepadHaptics: AndroidGamepadHaptics
    private val localBinder = LocalBinder()
    private var preparedRequest: RemoteSessionRequest? = null
    private var foregroundStarted = false
    private var foregroundDeviceName = "Pixels"
    private lateinit var audioManager: AudioManager
    private var audioFocusRequest: AudioFocusRequest? = null
    private var hasAudioFocus = false
    private var userWantsAudio = true
    private val mutableAudioEnabled = MutableStateFlow(false)
    private val mutableVoiceCallState = MutableStateFlow(VoiceCallState())
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
        clipboard = AndroidClipboardCoordinator(this, transport, serviceScope)
        recordings = AndroidRecordingCoordinator(this, transport, serviceScope)
        gamepadHaptics = AndroidGamepadHaptics(this)
        createNotificationChannel()
        serviceScope.launch {
            workflow.snapshot.collectLatest { snapshot ->
                (snapshot.clipboardDownload as? ClipboardDownloadState.Ready)?.let(clipboard::publish)
                if (snapshot.status is RemoteSessionStatus.Failed && foregroundStarted) {
                    gamepadHaptics.stop()
                    stopForeground(STOP_FOREGROUND_REMOVE)
                    foregroundStarted = false
                    stopSelf()
                }
            }
        }
        serviceScope.launch {
            transport.events.collect { event ->
                when (event) {
                    is RemoteTransportEvent.GamepadRumble -> {
                        val connected = workflow.snapshot.value.status as? RemoteSessionStatus.Connected
                        if (event.sessionId == connected?.request?.id) gamepadHaptics.apply(event.strongMotor, event.weakMotor)
                    }
                    is RemoteTransportEvent.Disconnected -> if (event.sessionId == currentRequest()?.id) gamepadHaptics.stop()
                    else -> Unit
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
        serviceScope.launch {
            transport.voiceCallEvents.collectLatest { event ->
                if (event.sessionId != currentRequest()?.id) return@collectLatest
                val speakerphone = mutableVoiceCallState.value.speakerphone
                mutableVoiceCallState.value = event.state.copy(speakerphone = speakerphone)
                if (event.state.phase == VoiceCallPhase.Idle) {
                    restoreVoiceAudioRoute()
                    refreshForegroundServiceTypes(voiceActive = false)
                }
                if (foregroundStarted) updateNotification(fileTransfers.tasks.value, recordings.state.value)
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
                transport.stopVoiceCall(it.id)
                fileTransfers.sessionEnded(it.id)
            }
            workflow.close()
        }
        recordings.close()
        gamepadHaptics.stop()
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

    private fun sendClipboardFiles(uris: List<Uri>) {
        val request = currentRequest() ?: return
        clipboard.share(request.id, uris)
    }

    private fun downloadClipboardFiles(files: RemoteClipboardFiles) {
        val request = currentRequest() ?: return
        clipboard.request(request.id, files)
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
            sessionId?.let { transport.stopVoiceCall(it) }
            sessionId?.let(fileTransfers::sessionEnded)
            workflow.stop()
            gamepadHaptics.stop()
            abandonAudioFocus()
            mutableAudioEnabled.value = false
            if (foregroundStarted) {
                stopForeground(STOP_FOREGROUND_REMOVE)
                foregroundStarted = false
            }
            stopSelf()
        }
    }

    private fun retrySession() {
        val failed = workflow.snapshot.value.status as? RemoteSessionStatus.Failed ?: return
        val request = failed.request
        preparedRequest = request
        userWantsAudio = request.enableAudio
        if (request.enableAudio) requestAudioFocus() else abandonAudioFocus()
        mutableAudioEnabled.value = request.enableAudio && hasAudioFocus
        startService(Intent(this, RemoteSessionService::class.java))
        startForegroundSession(request.target.displayName, request.enableAudio)
        serviceScope.launch {
            workflow.start(request)
            transport.setAudioEnabled(request.id, mutableAudioEnabled.value)
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

    private fun browseRemoteDirectory(path: String) {
        val connected = workflow.snapshot.value.status as? RemoteSessionStatus.Connected ?: return
        if (!connected.capabilities.supportsFileTransfer) return
        fileTransfers.browse(connected.request.id, path)
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

    private fun startVoiceCall() {
        val connected = workflow.snapshot.value.status as? RemoteSessionStatus.Connected ?: return
        if (!connected.capabilities.supportsVoiceCall) return
        if (ContextCompat.checkSelfPermission(this, android.Manifest.permission.RECORD_AUDIO) != PackageManager.PERMISSION_GRANTED) {
            mutableVoiceCallState.value = VoiceCallState(
                requiresHeadset = connected.capabilities.voiceCallRequiresHeadset,
                reason = "microphone_permission_required",
            )
            return
        }
        configureVoiceAudioRoute(mutableVoiceCallState.value.speakerphone)
        refreshForegroundServiceTypes(voiceActive = true)
        serviceScope.launch {
            if (!transport.startVoiceCall(connected.request.id)) {
                mutableVoiceCallState.value = VoiceCallState(
                    requiresHeadset = connected.capabilities.voiceCallRequiresHeadset,
                    reason = "voice_start_rejected",
                )
                restoreVoiceAudioRoute()
                refreshForegroundServiceTypes(voiceActive = false)
            }
        }
    }

    private fun stopVoiceCall() {
        val sessionId = currentRequest()?.id ?: return
        serviceScope.launch {
            transport.stopVoiceCall(sessionId)
            restoreVoiceAudioRoute()
            refreshForegroundServiceTypes(voiceActive = false)
        }
    }

    private fun setVoiceMicrophoneMuted(muted: Boolean) {
        val sessionId = currentRequest()?.id ?: return
        serviceScope.launch { transport.setVoiceMicrophoneMuted(sessionId, muted) }
    }

    private fun setVoiceSpeakerphone(enabled: Boolean) {
        if (mutableVoiceCallState.value.phase == VoiceCallPhase.Idle) return
        configureVoiceAudioRoute(enabled)
        mutableVoiceCallState.value = mutableVoiceCallState.value.copy(speakerphone = enabled)
    }

    private fun configureVoiceAudioRoute(speakerphone: Boolean) {
        audioManager.mode = AudioManager.MODE_IN_COMMUNICATION
        if (Build.VERSION.SDK_INT >= 31) {
            val preferredTypes = if (speakerphone) {
                listOf(AudioDeviceInfo.TYPE_BUILTIN_SPEAKER)
            } else {
                listOf(
                    AudioDeviceInfo.TYPE_BLE_HEADSET,
                    AudioDeviceInfo.TYPE_BLUETOOTH_SCO,
                    AudioDeviceInfo.TYPE_WIRED_HEADSET,
                    AudioDeviceInfo.TYPE_WIRED_HEADPHONES,
                    AudioDeviceInfo.TYPE_USB_HEADSET,
                    AudioDeviceInfo.TYPE_BUILTIN_EARPIECE,
                )
            }
            preferredTypes.asSequence()
                .mapNotNull { type -> audioManager.availableCommunicationDevices.firstOrNull { it.type == type } }
                .firstOrNull()
                ?.let(audioManager::setCommunicationDevice)
        } else {
            @Suppress("DEPRECATION")
            audioManager.isSpeakerphoneOn = speakerphone
        }
    }

    private fun restoreVoiceAudioRoute() {
        if (Build.VERSION.SDK_INT >= 31) audioManager.clearCommunicationDevice() else {
            @Suppress("DEPRECATION")
            audioManager.isSpeakerphoneOn = false
        }
        audioManager.mode = AudioManager.MODE_NORMAL
        val previous = mutableVoiceCallState.value
        if (previous.speakerphone) mutableVoiceCallState.value = previous.copy(speakerphone = false)
    }

    private fun refreshForegroundServiceTypes(voiceActive: Boolean) {
        if (!foregroundStarted) return
        val serviceTypes = ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE or
            (if (userWantsAudio) ServiceInfo.FOREGROUND_SERVICE_TYPE_MEDIA_PLAYBACK else 0) or
            (if (voiceActive) ServiceInfo.FOREGROUND_SERVICE_TYPE_MICROPHONE else 0)
        startForeground(NOTIFICATION_ID, createNotification(foregroundDeviceName), serviceTypes)
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
                    mutableVoiceCallState.value.phase != VoiceCallPhase.Idle -> getString(R.string.remote_notification_voice)
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

        val remoteDirectory: StateFlow<RemoteDirectoryState>
            get() = fileTransfers.remoteDirectory

        val recordingState: StateFlow<RecordingState>
            get() = recordings.state

        val voiceCallState: StateFlow<VoiceCallState>
            get() = mutableVoiceCallState.asStateFlow()

        fun prepare(request: RemoteSessionRequest) = this@RemoteSessionService.prepare(request)

        fun attachSurface(surface: Surface) = this@RemoteSessionService.attachSurface(surface)

        fun detachSurface(surface: Surface) = this@RemoteSessionService.detachSurface(surface)

        fun sendInput(command: InputCommand) = this@RemoteSessionService.sendInput(command)

        fun switchMonitor(monitorName: String) = this@RemoteSessionService.switchMonitor(monitorName)

        fun requestVirtualDisplay(requestId: String, operation: RemoteVirtualDisplayOperation) =
            this@RemoteSessionService.requestVirtualDisplay(requestId, operation)

        fun sendText(text: String) = this@RemoteSessionService.sendText(text)

        fun sendClipboardText(text: String) = this@RemoteSessionService.sendClipboardText(text)

        fun sendClipboardFiles(uris: List<Uri>) = this@RemoteSessionService.sendClipboardFiles(uris)

        fun downloadClipboardFiles(files: RemoteClipboardFiles) = this@RemoteSessionService.downloadClipboardFiles(files)

        fun setAudioEnabled(enabled: Boolean) = this@RemoteSessionService.setAudioEnabled(enabled)

        fun startUpload(source: Uri, remoteDirectory: String) = this@RemoteSessionService.startUpload(source, remoteDirectory)

        fun browseRemoteDirectory(path: String) = this@RemoteSessionService.browseRemoteDirectory(path)

        fun startDownload(remotePath: String, destination: Uri) = this@RemoteSessionService.startDownload(remotePath, destination)

        fun cancelTransfer(taskId: String) = fileTransfers.cancel(taskId)

        fun retryTransfer(taskId: String) = fileTransfers.retry(taskId)

        fun resolveTransferOverwrite(taskId: String, overwrite: Boolean, applyToAll: Boolean) =
            fileTransfers.resolveOverwrite(taskId, overwrite, applyToAll)

        fun clearFinishedTransfers() = fileTransfers.clearFinished()

        fun startRecording() = this@RemoteSessionService.startRecording()

        fun stopRecording() = recordings.stop()

        fun startVoiceCall() = this@RemoteSessionService.startVoiceCall()

        fun stopVoiceCall() = this@RemoteSessionService.stopVoiceCall()

        fun setVoiceMicrophoneMuted(muted: Boolean) = this@RemoteSessionService.setVoiceMicrophoneMuted(muted)

        fun setVoiceSpeakerphone(enabled: Boolean) = this@RemoteSessionService.setVoiceSpeakerphone(enabled)

        fun stopSession() = this@RemoteSessionService.stopSession()

        fun retrySession() = this@RemoteSessionService.retrySession()
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
