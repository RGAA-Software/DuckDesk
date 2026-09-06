@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package yun.pixels.client.ui

import android.Manifest
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.os.Build
import android.os.IBinder
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Devices
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.SwapVert
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavDestination.Companion.hierarchy
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import kotlinx.coroutines.launch
import yun.pixels.client.PixelsAppGraph
import yun.pixels.client.R
import yun.pixels.client.feature.devices.DeviceHomeAction
import yun.pixels.client.feature.devices.DeviceHomeNotice
import yun.pixels.client.feature.devices.DeviceHomeScreen
import yun.pixels.client.feature.devices.DeviceHomeViewModel
import yun.pixels.client.feature.devices.ApplicationLibraryScreen
import yun.pixels.client.feature.devices.ApplicationLibraryViewModel
import yun.pixels.client.feature.settings.SettingsScreen
import yun.pixels.client.feature.settings.SettingsViewModel
import yun.pixels.client.core.domain.session.RemoteSessionRequest
import yun.pixels.client.core.domain.session.RemoteSessionSnapshot
import yun.pixels.client.core.domain.session.RemoteSessionStatus
import yun.pixels.client.core.domain.session.RemoteSessionTarget
import yun.pixels.client.feature.remote.RemoteWorkspaceScreen
import yun.pixels.client.feature.transfer.TransferScreen
import yun.pixels.client.remote.RemoteSessionService
import yun.pixels.client.core.domain.transfer.FileTransferTask
import yun.pixels.client.core.domain.recording.RecordingState
import yun.pixels.client.core.domain.voice.VoiceCallState

private enum class TopLevelDestination(
    val route: String,
    @StringRes val labelResource: Int,
    val icon: ImageVector,
) {
    Devices("devices", R.string.navigation_devices, Icons.Outlined.Devices),
    Transfers("transfers", R.string.navigation_transfers, Icons.Outlined.SwapVert),
    Settings("settings", R.string.navigation_settings, Icons.Outlined.Settings),
}
private const val REMOTE_ROUTE = "remote"
private const val APPLICATIONS_ROUTE = "applications"

@Composable
fun PixelsApp(graph: PixelsAppGraph) {
    val navController = rememberNavController()
    val snackbarHostState = remember { SnackbarHostState() }
    val coroutineScope = rememberCoroutineScope()
    val clipboard = LocalClipboard.current
    val context = LocalContext.current
    var remoteBinder by remember { mutableStateOf<RemoteSessionService.LocalBinder?>(null) }
    var remoteRequest by remember { mutableStateOf<RemoteSessionRequest?>(null) }
    var remoteRequestAwaitingLocalNetwork by remember { mutableStateOf<RemoteSessionRequest?>(null) }
    val idleRemoteSnapshot = remember { kotlinx.coroutines.flow.MutableStateFlow(RemoteSessionSnapshot()) }
    val idleAudioEnabled = remember { kotlinx.coroutines.flow.MutableStateFlow(false) }
    val idleFileTransferTasks = remember { kotlinx.coroutines.flow.MutableStateFlow(emptyList<FileTransferTask>()) }
    val idleRecordingState = remember { kotlinx.coroutines.flow.MutableStateFlow<RecordingState>(RecordingState.Idle) }
    val idleVoiceCallState = remember { kotlinx.coroutines.flow.MutableStateFlow(VoiceCallState()) }
    DisposableEffect(context) {
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName, service: IBinder) {
                remoteBinder = service as RemoteSessionService.LocalBinder
            }

            override fun onServiceDisconnected(name: ComponentName) {
                remoteBinder = null
            }
        }
        context.bindService(Intent(context, RemoteSessionService::class.java), connection, Context.BIND_AUTO_CREATE)
        onDispose {
            runCatching { context.unbindService(connection) }
            remoteBinder = null
        }
    }
    val codeScanner = remember(context) {
        val options = GmsBarcodeScannerOptions.Builder()
            .setBarcodeFormats(Barcode.FORMAT_QR_CODE)
            .enableAutoZoom()
            .build()
        GmsBarcodeScanning.getClient(context, options)
    }
    val deviceHomeViewModel: DeviceHomeViewModel = viewModel(
        factory = DeviceHomeViewModel.factory(
            graph.deviceDirectory,
            graph.deviceResolver,
            graph.deviceDiscovery,
            graph.accountRepository,
        ),
    )
    val deviceHomeState by deviceHomeViewModel.uiState.collectAsStateWithLifecycle()
    val applicationLibraryViewModel: ApplicationLibraryViewModel = viewModel(
        factory = ApplicationLibraryViewModel.factory(graph.applicationRepository),
    )
    val applicationLibraryState by applicationLibraryViewModel.state.collectAsStateWithLifecycle()
    val settingsViewModel: SettingsViewModel = viewModel(
        factory = SettingsViewModel.factory(graph.accountRepository),
    )
    val settingsState by settingsViewModel.uiState.collectAsStateWithLifecycle()
    val currentBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = currentBackStackEntry?.destination
    val unavailableMessage = stringResource(R.string.feature_being_built)
    var pendingLocalNetworkAction by remember { mutableStateOf<DeviceHomeAction?>(null) }
    val noticeMessages by rememberUpdatedState(
        mapOf(
            DeviceHomeNotice.DeviceSaved to stringResource(R.string.device_saved),
            DeviceHomeNotice.DeviceRemoved to stringResource(R.string.device_removed),
            DeviceHomeNotice.LocalNetworkPermissionRequired to stringResource(R.string.local_network_permission_required),
            DeviceHomeNotice.FeatureUnavailable to unavailableMessage,
            DeviceHomeNotice.DiscoveryFinished to stringResource(R.string.discovery_finished),
            DeviceHomeNotice.NoDevicesDiscovered to stringResource(R.string.no_devices_discovered),
            DeviceHomeNotice.ScannerUnavailable to stringResource(R.string.scanner_unavailable),
            DeviceHomeNotice.RemoteConnectionUnavailable to stringResource(R.string.remote_connection_unavailable),
        ),
    )
    val localNetworkPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission(),
    ) { granted ->
        val action = pendingLocalNetworkAction
        pendingLocalNetworkAction = null
        deviceHomeViewModel.onAction(if (granted && action != null) action else DeviceHomeAction.LocalNetworkPermissionDenied)
    }
    val notificationPermissionLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { }
    val microphonePermissionDenied = stringResource(R.string.microphone_permission_required)
    val microphonePermissionLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        if (granted) remoteBinder?.startVoiceCall() else coroutineScope.launch { snackbarHostState.showSnackbar(microphonePermissionDenied) }
    }
    var pendingUploadRemoteDirectory by remember { mutableStateOf("") }
    var pendingDownloadRemotePath by remember { mutableStateOf("") }
    val uploadDocumentLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { source ->
        val remoteDirectory = pendingUploadRemoteDirectory
        pendingUploadRemoteDirectory = ""
        if (source != null && remoteDirectory.isNotBlank()) {
            runCatching { context.contentResolver.takePersistableUriPermission(source, Intent.FLAG_GRANT_READ_URI_PERMISSION) }
            remoteBinder?.startUpload(source, remoteDirectory)
        }
    }
    val downloadDocumentLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/octet-stream")) { destination ->
        val remotePath = pendingDownloadRemotePath
        pendingDownloadRemotePath = ""
        if (destination != null && remotePath.isNotBlank()) {
            runCatching { context.contentResolver.takePersistableUriPermission(destination, Intent.FLAG_GRANT_WRITE_URI_PERMISSION) }
            remoteBinder?.startDownload(remotePath, destination)
        }
    }
    val remoteLocalNetworkPermissionLauncher = rememberLauncherForActivityResult(ActivityResultContracts.RequestPermission()) { granted ->
        val request = remoteRequestAwaitingLocalNetwork
        remoteRequestAwaitingLocalNetwork = null
        if (granted && request != null) {
            remoteRequest = request
        } else {
            deviceHomeViewModel.onAction(DeviceHomeAction.LocalNetworkPermissionDenied)
        }
    }

    LaunchedEffect(deviceHomeViewModel) {
        deviceHomeViewModel.notices.collect { notice ->
            snackbarHostState.showSnackbar(noticeMessages.getValue(notice))
        }
    }
    LaunchedEffect(deviceHomeViewModel) {
        deviceHomeViewModel.remoteRequests.collect { request -> remoteRequest = request }
    }
    LaunchedEffect(applicationLibraryViewModel) {
        applicationLibraryViewModel.remoteRequests.collect { request -> remoteRequest = request }
    }
    LaunchedEffect(remoteBinder, remoteRequest) {
        val binder = remoteBinder ?: return@LaunchedEffect
        val request = remoteRequest ?: return@LaunchedEffect
        val requiresLocalNetwork = request.target is RemoteSessionTarget.Direct ||
            (request.target as? RemoteSessionTarget.Account)?.connectionTicket?.launchUrl?.startsWith("http://", ignoreCase = true) == true
        if (Build.VERSION.SDK_INT >= 37 && requiresLocalNetwork && ContextCompat.checkSelfPermission(
                context,
                Manifest.permission.ACCESS_LOCAL_NETWORK,
            ) != PackageManager.PERMISSION_GRANTED
        ) {
            remoteRequestAwaitingLocalNetwork = request
            remoteRequest = null
            remoteLocalNetworkPermissionLauncher.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
            return@LaunchedEffect
        }
        if (Build.VERSION.SDK_INT >= 33 && ContextCompat.checkSelfPermission(context, Manifest.permission.POST_NOTIFICATIONS) !=
            PackageManager.PERMISSION_GRANTED
        ) {
            notificationPermissionLauncher.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        binder.prepare(request)
        remoteRequest = null
        navController.navigate(REMOTE_ROUTE) { launchSingleTop = true }
    }
    LaunchedEffect(remoteBinder) {
        val status = remoteBinder?.snapshot?.value?.status ?: return@LaunchedEffect
        if (status !is RemoteSessionStatus.Idle && currentDestination?.route != REMOTE_ROUTE) {
            navController.navigate(REMOTE_ROUTE) { launchSingleTop = true }
        }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        bottomBar = {
            if (currentDestination?.route !in setOf(REMOTE_ROUTE, APPLICATIONS_ROUTE)) {
                NavigationBar {
                    TopLevelDestination.values().forEach { destination ->
                        val selected = currentDestination?.hierarchy?.any { it.route == destination.route } == true
                        NavigationBarItem(
                            selected = selected,
                            onClick = {
                                navController.navigate(destination.route) {
                                    popUpTo(TopLevelDestination.Devices.route) { saveState = true }
                                    launchSingleTop = true
                                    restoreState = true
                                }
                            },
                            icon = { Icon(imageVector = destination.icon, contentDescription = null) },
                            label = { Text(text = stringResource(destination.labelResource)) },
                        )
                    }
                }
            }
        },
    ) { contentPadding ->
        NavHost(
            navController = navController,
            startDestination = TopLevelDestination.Devices.route,
            modifier = Modifier.padding(
                if (currentDestination?.route in setOf(REMOTE_ROUTE, APPLICATIONS_ROUTE)) PaddingValues(0.dp) else contentPadding,
            ),
        ) {
            composable(TopLevelDestination.Devices.route) {
                DeviceHomeScreen(
                    state = deviceHomeState,
                    onAction = { action ->
                        when (action) {
                            DeviceHomeAction.Paste -> {
                                coroutineScope.launch {
                                    val clipData = clipboard.getClipEntry()?.clipData
                                    val pastedValue = if (clipData != null && clipData.itemCount > 0) {
                                        clipData.getItemAt(0).coerceToText(context).toString()
                                    } else {
                                        ""
                                    }
                                    deviceHomeViewModel.onAction(DeviceHomeAction.ConnectionInputChanged(pastedValue))
                                }
                            }
                            DeviceHomeAction.Connect, DeviceHomeAction.DiscoverLocal -> {
                                val permissionRequired = Build.VERSION.SDK_INT >= 37 && ContextCompat.checkSelfPermission(
                                    context,
                                    Manifest.permission.ACCESS_LOCAL_NETWORK,
                                ) != PackageManager.PERMISSION_GRANTED
                                if (permissionRequired) {
                                    pendingLocalNetworkAction = action
                                    localNetworkPermissionLauncher.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
                                } else {
                                    deviceHomeViewModel.onAction(action)
                                }
                            }
                            DeviceHomeAction.ScanCode -> codeScanner.startScan()
                                .addOnSuccessListener { barcode ->
                                    val value = barcode.rawValue.orEmpty().trim()
                                    if (value.isEmpty()) {
                                        deviceHomeViewModel.onAction(DeviceHomeAction.ScannerFailed)
                                    } else {
                                        deviceHomeViewModel.onAction(DeviceHomeAction.ConnectionInputChanged(value))
                                        val permissionRequired = Build.VERSION.SDK_INT >= 37 && ContextCompat.checkSelfPermission(
                                            context,
                                            Manifest.permission.ACCESS_LOCAL_NETWORK,
                                        ) != PackageManager.PERMISSION_GRANTED
                                        if (permissionRequired) {
                                            pendingLocalNetworkAction = DeviceHomeAction.Connect
                                            localNetworkPermissionLauncher.launch(Manifest.permission.ACCESS_LOCAL_NETWORK)
                                        } else {
                                            deviceHomeViewModel.onAction(DeviceHomeAction.Connect)
                                        }
                                    }
                                }
                                .addOnFailureListener { deviceHomeViewModel.onAction(DeviceHomeAction.ScannerFailed) }
                            DeviceHomeAction.OpenAccountSettings -> navController.navigate(TopLevelDestination.Settings.route) {
                                launchSingleTop = true
                            }
                            DeviceHomeAction.OpenApplications -> {
                                applicationLibraryViewModel.refresh()
                                navController.navigate(APPLICATIONS_ROUTE) { launchSingleTop = true }
                            }
                            else -> deviceHomeViewModel.onAction(action)
                        }
                    },
                )
            }
            composable(TopLevelDestination.Transfers.route) {
                val transferTasksFlow = remoteBinder?.fileTransferTasks ?: idleFileTransferTasks
                val transferTasks by transferTasksFlow.collectAsStateWithLifecycle()
                val sessionFlow = remoteBinder?.snapshot ?: idleRemoteSnapshot
                val transferSnapshot by sessionFlow.collectAsStateWithLifecycle()
                val connected = transferSnapshot.status as? RemoteSessionStatus.Connected
                TransferScreen(
                    tasks = transferTasks,
                    sessionConnected = connected != null,
                    supportsFileTransfer = connected?.capabilities?.supportsFileTransfer == true,
                    onChooseUpload = { remoteDirectory ->
                        pendingUploadRemoteDirectory = remoteDirectory
                        uploadDocumentLauncher.launch(arrayOf("*/*"))
                    },
                    onChooseDownloadDestination = { remotePath ->
                        pendingDownloadRemotePath = remotePath
                        downloadDocumentLauncher.launch(remotePath.substringAfterLast('/').substringAfterLast('\\').ifBlank { "download" })
                    },
                    onCancel = { taskId -> remoteBinder?.cancelTransfer(taskId) },
                    onRetry = { taskId -> remoteBinder?.retryTransfer(taskId) },
                    onResolveOverwrite = { taskId, overwrite, applyToAll ->
                        remoteBinder?.resolveTransferOverwrite(taskId, overwrite, applyToAll)
                    },
                    onClearFinished = { remoteBinder?.clearFinishedTransfers() },
                )
            }
            composable(TopLevelDestination.Settings.route) {
                SettingsScreen(state = settingsState, onAction = settingsViewModel::onAction)
            }
            composable(APPLICATIONS_ROUTE) {
                ApplicationLibraryScreen(
                    state = applicationLibraryState,
                    onBack = { navController.popBackStack() },
                    onRefresh = applicationLibraryViewModel::refresh,
                    onStart = applicationLibraryViewModel::start,
                    onConnect = applicationLibraryViewModel::connect,
                    onStop = applicationLibraryViewModel::stop,
                )
            }
            composable(REMOTE_ROUTE) {
                val sessionFlow = remoteBinder?.snapshot ?: idleRemoteSnapshot
                val snapshot by sessionFlow.collectAsStateWithLifecycle()
                val audioEnabledFlow = remoteBinder?.audioEnabled ?: idleAudioEnabled
                val audioEnabled by audioEnabledFlow.collectAsStateWithLifecycle()
                val recordingStateFlow = remoteBinder?.recordingState ?: idleRecordingState
                val recordingState by recordingStateFlow.collectAsStateWithLifecycle()
                val voiceCallStateFlow = remoteBinder?.voiceCallState ?: idleVoiceCallState
                val voiceCallState by voiceCallStateFlow.collectAsStateWithLifecycle()
                RemoteWorkspaceScreen(
                    snapshot = snapshot,
                    audioEnabled = audioEnabled,
                    recordingState = recordingState,
                    voiceCallState = voiceCallState,
                    surfaceConsumerReady = remoteBinder != null,
                    onSurfaceAvailable = { surface -> remoteBinder?.attachSurface(surface) },
                    onSurfaceDestroyed = { surface -> remoteBinder?.detachSurface(surface) },
                    onInput = { command -> remoteBinder?.sendInput(command) },
                    onSwitchMonitor = { monitorName -> remoteBinder?.switchMonitor(monitorName) },
                    onVirtualDisplayRequest = { requestId, operation -> remoteBinder?.requestVirtualDisplay(requestId, operation) },
                    onText = { text -> remoteBinder?.sendText(text) },
                    onClipboardText = { text -> remoteBinder?.sendClipboardText(text) },
                    onAudioEnabledChange = { enabled -> remoteBinder?.setAudioEnabled(enabled) },
                    onStartRecording = { remoteBinder?.startRecording() },
                    onStopRecording = { remoteBinder?.stopRecording() },
                    onStartVoiceCall = {
                        if (ContextCompat.checkSelfPermission(context, Manifest.permission.RECORD_AUDIO) == PackageManager.PERMISSION_GRANTED) {
                            remoteBinder?.startVoiceCall()
                        } else {
                            microphonePermissionLauncher.launch(Manifest.permission.RECORD_AUDIO)
                        }
                    },
                    onStopVoiceCall = { remoteBinder?.stopVoiceCall() },
                    onVoiceMicrophoneMuted = { muted -> remoteBinder?.setVoiceMicrophoneMuted(muted) },
                    onVoiceSpeakerphone = { enabled -> remoteBinder?.setVoiceSpeakerphone(enabled) },
                    onOpenTransfers = { navController.navigate(TopLevelDestination.Transfers.route) },
                    onEndSession = {
                        remoteBinder?.stopSession()
                        navController.popBackStack()
                    },
                )
            }
        }
    }
}
