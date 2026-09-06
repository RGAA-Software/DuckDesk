@file:OptIn(androidx.compose.material3.ExperimentalMaterial3Api::class)

package yun.pixels.client.feature.devices

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.outlined.Add
import androidx.compose.material.icons.outlined.Apps
import androidx.compose.material.icons.outlined.Computer
import androidx.compose.material.icons.outlined.ContentPaste
import androidx.compose.material.icons.outlined.DeleteOutline
import androidx.compose.material.icons.outlined.Folder
import androidx.compose.material.icons.outlined.Link
import androidx.compose.material.icons.outlined.MoreVert
import androidx.compose.material.icons.outlined.QrCodeScanner
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.tooling.preview.Preview
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.RemoteDevice

@Composable
fun DeviceHomeScreen(
    state: DeviceHomeUiState,
    onAction: (DeviceHomeAction) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier = modifier.fillMaxSize()) {
        TopAppBar(
            title = {
                Row(
                    horizontalArrangement = Arrangement.spacedBy(12.dp),
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    PixelsMark()
                    Text(
                        text = stringResource(R.string.device_home_title),
                        style = MaterialTheme.typography.titleLarge,
                        fontWeight = FontWeight.SemiBold,
                    )
                }
            },
        )

        LazyColumn(
            contentPadding = PaddingValues(start = 16.dp, top = 8.dp, end = 16.dp, bottom = 112.dp),
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            item {
                QuickConnectCard(state = state, onAction = onAction)
            }
            item {
                Row(
                    modifier = Modifier.fillMaxWidth(),
                    horizontalArrangement = Arrangement.SpaceBetween,
                    verticalAlignment = Alignment.CenterVertically,
                ) {
                    Text(
                        text = stringResource(R.string.my_devices),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold,
                    )
                    TextButton(onClick = { onAction(DeviceHomeAction.AddDevice) }) {
                        Icon(imageVector = Icons.Outlined.Add, contentDescription = null)
                        Text(text = stringResource(R.string.add_device))
                    }
                }
            }

            if (state.devices.isEmpty()) {
                item {
                    EmptyDevicesCard(onScan = { onAction(DeviceHomeAction.Scan) })
                }
            } else {
                items(items = state.devices, key = { it.id.value }) { device ->
                    DeviceCard(device = device, onAction = onAction)
                }
            }
        }
    }
}

@Composable
private fun QuickConnectCard(
    state: DeviceHomeUiState,
    onAction: (DeviceHomeAction) -> Unit,
) {
    Card(
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant),
        shape = RoundedCornerShape(20.dp),
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Text(
                text = stringResource(R.string.quick_connect),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
            )
            OutlinedTextField(
                value = state.connectionInput,
                onValueChange = { onAction(DeviceHomeAction.ConnectionInputChanged(it)) },
                modifier = Modifier.fillMaxWidth(),
                singleLine = true,
                isError = state.inputError != null,
                enabled = !state.isConnecting,
                placeholder = { Text(stringResource(R.string.connection_input_hint)) },
                supportingText = state.inputError?.let {
                    { Text(text = stringResource(it.labelResource())) }
                },
            )
            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceBetween,
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Row {
                    TextButton(onClick = { onAction(DeviceHomeAction.Paste) }) {
                        Icon(imageVector = Icons.Outlined.ContentPaste, contentDescription = null)
                        Text(text = stringResource(R.string.paste))
                    }
                    TextButton(onClick = { onAction(DeviceHomeAction.Scan) }) {
                        Icon(imageVector = Icons.Outlined.QrCodeScanner, contentDescription = null)
                        Text(text = stringResource(R.string.scan))
                    }
                }
                Button(
                    onClick = { onAction(DeviceHomeAction.Connect) },
                    enabled = !state.isConnecting,
                ) {
                    if (state.isConnecting) {
                        CircularProgressIndicator(modifier = Modifier.size(18.dp), strokeWidth = 2.dp)
                    } else {
                        Icon(imageVector = Icons.Outlined.Link, contentDescription = null)
                    }
                    Text(text = stringResource(if (state.isConnecting) R.string.connecting else R.string.connect))
                }
            }
        }
    }
}

@Composable
private fun EmptyDevicesCard(onScan: () -> Unit) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(20.dp),
    ) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 24.dp, vertical = 32.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
        ) {
            Icon(
                imageVector = Icons.Outlined.Computer,
                contentDescription = null,
                modifier = Modifier.size(48.dp),
                tint = MaterialTheme.colorScheme.primary,
            )
            Spacer(modifier = Modifier.height(16.dp))
            Text(
                text = stringResource(R.string.no_devices_title),
                style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold,
            )
            Spacer(modifier = Modifier.height(8.dp))
            Text(
                text = stringResource(R.string.no_devices_body),
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                style = MaterialTheme.typography.bodyMedium,
            )
            Spacer(modifier = Modifier.height(20.dp))
            FilledTonalButton(onClick = onScan) {
                Icon(imageVector = Icons.Outlined.QrCodeScanner, contentDescription = null)
                Text(text = stringResource(R.string.scan_to_connect))
            }
        }
    }
}

@Composable
private fun DeviceCard(
    device: RemoteDevice,
    onAction: (DeviceHomeAction) -> Unit,
) {
    var menuExpanded by remember { mutableStateOf(false) }
    Card(
        onClick = { onAction(DeviceHomeAction.OpenDevice(device)) },
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surface),
        shape = RoundedCornerShape(18.dp),
    ) {
        Column(
            modifier = Modifier.padding(20.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp),
        ) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                AvailabilityDot(device.availability)
                Text(
                    text = device.displayName,
                    modifier = Modifier
                        .weight(1f)
                        .padding(start = 10.dp),
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.SemiBold,
                )
                Box {
                    IconButton(onClick = { menuExpanded = true }) {
                        Icon(
                            imageVector = Icons.Outlined.MoreVert,
                            contentDescription = stringResource(R.string.more_actions),
                        )
                    }
                    DropdownMenu(
                        expanded = menuExpanded,
                        onDismissRequest = { menuExpanded = false },
                    ) {
                        DropdownMenuItem(
                            text = { Text(stringResource(R.string.remove_device)) },
                            leadingIcon = { Icon(Icons.Outlined.DeleteOutline, contentDescription = null) },
                            onClick = {
                                menuExpanded = false
                                onAction(DeviceHomeAction.RemoveDevice(device))
                            },
                        )
                    }
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                Text(
                    text = device.platformName,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    text = device.endpoint.host,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.bodySmall,
                )
                Text(
                    text = stringResource(device.availability.labelResource()),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    style = MaterialTheme.typography.bodySmall,
                )
                device.latencyMillis?.let { latency ->
                    Text(
                        text = stringResource(R.string.latency_millis, latency),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        style = MaterialTheme.typography.bodySmall,
                    )
                }
            }
            if (device.availability == DeviceAvailability.Online) {
                Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                    Button(onClick = { onAction(DeviceHomeAction.StartRemoteDesktop(device)) }) {
                        Icon(imageVector = Icons.Outlined.Computer, contentDescription = null)
                        Text(text = stringResource(R.string.remote_desktop))
                    }
                    FilledTonalButton(onClick = { onAction(DeviceHomeAction.OpenFiles(device)) }) {
                        Icon(imageVector = Icons.Outlined.Folder, contentDescription = stringResource(R.string.files))
                    }
                    FilledTonalButton(onClick = { onAction(DeviceHomeAction.OpenApplications(device)) }) {
                        Icon(imageVector = Icons.Outlined.Apps, contentDescription = stringResource(R.string.applications))
                    }
                }
            }
        }
    }
}

@Composable
private fun AvailabilityDot(availability: DeviceAvailability) {
    val color = when (availability) {
        DeviceAvailability.Online -> MaterialTheme.colorScheme.secondary
        DeviceAvailability.Offline -> MaterialTheme.colorScheme.outline
        DeviceAvailability.Discovering -> MaterialTheme.colorScheme.tertiary
    }
    Canvas(modifier = Modifier.size(10.dp)) {
        drawCircle(color = color)
    }
}

private fun DeviceAvailability.labelResource(): Int = when (this) {
    DeviceAvailability.Online -> R.string.online
    DeviceAvailability.Offline -> R.string.offline
    DeviceAvailability.Discovering -> R.string.discovering
}

private fun ConnectionInputError.labelResource(): Int = when (this) {
    ConnectionInputError.Empty -> R.string.connection_input_empty
    ConnectionInputError.Invalid -> R.string.connection_input_invalid
    ConnectionInputError.PublicNetworkAddress -> R.string.connection_input_public_address
    ConnectionInputError.Unreachable -> R.string.connection_input_unreachable
    ConnectionInputError.InvalidResponse -> R.string.connection_input_invalid_response
}

@Composable
private fun PixelsMark(modifier: Modifier = Modifier) {
    val primary = MaterialTheme.colorScheme.primary
    val secondary = MaterialTheme.colorScheme.secondary
    Box(modifier = modifier.size(36.dp)) {
        Canvas(modifier = Modifier.fillMaxSize()) {
            drawPixelsMark(primary = primary, secondary = secondary)
        }
    }
}

private fun DrawScope.drawPixelsMark(primary: Color, secondary: Color) {
    val cells = listOf(
        0 to 0, 1 to 0, 2 to 0,
        0 to 1, 3 to 1,
        0 to 2, 1 to 2, 2 to 2,
        0 to 3,
        0 to 4,
    )
    val cell = size.minDimension / 5f
    val gap = cell * 0.18f
    val side = cell - gap
    cells.forEach { (column, row) ->
        drawRoundRect(
            color = primary,
            topLeft = Offset(column * cell, row * cell),
            size = Size(side, side),
            cornerRadius = CornerRadius(side * 0.18f),
        )
    }
    drawRoundRect(
        color = secondary,
        topLeft = Offset(3f * cell, 4f * cell),
        size = Size(side, side),
        cornerRadius = CornerRadius(side * 0.18f),
    )
}

@Preview(showBackground = true, backgroundColor = 0xFF080C18)
@Composable
private fun DeviceHomePreview() {
    MaterialTheme {
        DeviceHomeScreen(
            state = DeviceHomeUiState(
                devices = listOf(
                    RemoteDevice(
                        id = DeviceId("office-pc"),
                        displayName = "Office PC",
                        platformName = "Windows 11",
                        availability = DeviceAvailability.Online,
                        endpoint = yun.pixels.client.core.domain.device.DeviceEndpoint("192.168.1.18"),
                        latencyMillis = 12,
                    ),
                ),
            ),
            onAction = {},
        )
    }
}
