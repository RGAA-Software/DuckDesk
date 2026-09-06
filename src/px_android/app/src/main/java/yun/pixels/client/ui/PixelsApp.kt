@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package yun.pixels.client.ui

import android.Manifest
import android.content.pm.PackageManager
import android.os.Build
import com.google.mlkit.vision.barcode.common.Barcode
import com.google.mlkit.vision.codescanner.GmsBarcodeScannerOptions
import com.google.mlkit.vision.codescanner.GmsBarcodeScanning
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.annotation.StringRes
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Devices
import androidx.compose.material.icons.outlined.Settings
import androidx.compose.material.icons.outlined.SwapVert
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.platform.LocalClipboard
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
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
import yun.pixels.client.feature.settings.SettingsScreen
import yun.pixels.client.feature.settings.SettingsViewModel

private enum class TopLevelDestination(
    val route: String,
    @StringRes val labelResource: Int,
    val icon: ImageVector,
) {
    Devices("devices", R.string.navigation_devices, Icons.Outlined.Devices),
    Transfers("transfers", R.string.navigation_transfers, Icons.Outlined.SwapVert),
    Settings("settings", R.string.navigation_settings, Icons.Outlined.Settings),
}

@Composable
fun PixelsApp(graph: PixelsAppGraph) {
    val navController = rememberNavController()
    val snackbarHostState = remember { SnackbarHostState() }
    val coroutineScope = rememberCoroutineScope()
    val clipboard = LocalClipboard.current
    val context = LocalContext.current
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
        ),
    )
    val localNetworkPermissionLauncher = rememberLauncherForActivityResult(
        contract = ActivityResultContracts.RequestPermission(),
    ) { granted ->
        val action = pendingLocalNetworkAction
        pendingLocalNetworkAction = null
        deviceHomeViewModel.onAction(if (granted && action != null) action else DeviceHomeAction.LocalNetworkPermissionDenied)
    }

    LaunchedEffect(deviceHomeViewModel) {
        deviceHomeViewModel.notices.collect { notice ->
            snackbarHostState.showSnackbar(noticeMessages.getValue(notice))
        }
    }

    Scaffold(
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        bottomBar = {
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
        },
    ) { contentPadding ->
        NavHost(
            navController = navController,
            startDestination = TopLevelDestination.Devices.route,
            modifier = Modifier.padding(contentPadding),
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
                            else -> deviceHomeViewModel.onAction(action)
                        }
                    },
                )
            }
            composable(TopLevelDestination.Transfers.route) {
                FoundationScreen(
                    title = stringResource(R.string.transfers_title),
                    body = stringResource(R.string.transfers_foundation_body),
                )
            }
            composable(TopLevelDestination.Settings.route) {
                SettingsScreen(state = settingsState, onAction = settingsViewModel::onAction)
            }
        }
    }
}

@Composable
private fun FoundationScreen(title: String, body: String) {
    Column(modifier = Modifier.fillMaxSize()) {
        TopAppBar(
            title = {
                Text(
                    text = title,
                    fontWeight = FontWeight.SemiBold,
                )
            },
        )
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center,
        ) {
            Text(
                text = body,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyLarge,
            )
        }
    }
}
