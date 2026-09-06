package yun.pixels.client.feature.remote

import android.os.SystemClock
import android.view.KeyEvent
import android.view.InputDevice
import android.view.MotionEvent
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import androidx.activity.compose.BackHandler
import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.outlined.VolumeOff
import androidx.compose.material.icons.automirrored.outlined.VolumeUp
import androidx.compose.material.icons.outlined.Close
import androidx.compose.material.icons.outlined.DesktopWindows
import androidx.compose.material.icons.outlined.Keyboard
import androidx.compose.material.icons.outlined.Mouse
import androidx.compose.material.icons.outlined.SportsEsports
import androidx.compose.material.icons.outlined.TouchApp
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilledTonalIconButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Slider
import androidx.compose.material3.Surface as MaterialSurface
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.viewinterop.AndroidView
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteGamepadButton
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteKey
import yun.pixels.client.core.domain.session.RemoteMouseButton
import yun.pixels.client.core.domain.session.RemoteSessionSnapshot
import yun.pixels.client.core.domain.session.RemoteSessionStatus
import yun.pixels.client.core.domain.session.RemoteVirtualDisplayOperation

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RemoteWorkspaceScreen(
    snapshot: RemoteSessionSnapshot,
    audioEnabled: Boolean,
    surfaceConsumerReady: Boolean,
    onSurfaceAvailable: (Surface) -> Unit,
    onSurfaceDestroyed: (Surface) -> Unit,
    onInput: (InputCommand) -> Unit,
    onSwitchMonitor: (String) -> Unit,
    onVirtualDisplayRequest: (String, RemoteVirtualDisplayOperation) -> Unit,
    onText: (String) -> Unit,
    onAudioEnabledChange: (Boolean) -> Unit,
    onEndSession: () -> Unit,
) {
    BackHandler(onBack = onEndSession)
    val context = LocalContext.current
    val preferences = remember(context) { context.getSharedPreferences(INPUT_PREFERENCES, 0) }
    var inputMode by remember {
        mutableStateOf(
            runCatching { RemoteInputMode.valueOf(preferences.getString(INPUT_MODE, null).orEmpty()) }
                .getOrDefault(RemoteInputMode.DirectTouch),
        )
    }
    var sensitivity by remember { mutableStateOf(preferences.getFloat(INPUT_SENSITIVITY, 1f).coerceIn(0.5f, 2f)) }
    var showKeyboard by remember { mutableStateOf(false) }
    var showDisplays by remember { mutableStateOf(false) }
    var allowPortraitGamepad by remember { mutableStateOf(false) }
    var pendingVirtualDisplayRequest by remember { mutableStateOf<String?>(null) }
    var confirmSecureAttention by remember { mutableStateOf(false) }
    var currentSurface by remember { mutableStateOf<Surface?>(null) }
    val latestSurfaceAvailable by rememberUpdatedState(onSurfaceAvailable)
    val latestSurfaceDestroyed by rememberUpdatedState(onSurfaceDestroyed)
    val latestInput by rememberUpdatedState(onInput)
    val latestMode by rememberUpdatedState(inputMode)
    val latestSensitivity by rememberUpdatedState(sensitivity)
    val interpreter = remember { RemoteGestureInterpreter { latestInput(it) } }
    val gamepadController = remember { RemoteGamepadController { latestInput(it) } }
    interpreter.mode = inputMode
    interpreter.touchpadSensitivity = sensitivity
    LaunchedEffect(currentSurface, surfaceConsumerReady) {
        currentSurface?.takeIf { surfaceConsumerReady && it.isValid }?.let(latestSurfaceAvailable)
    }
    LaunchedEffect(snapshot.lastVirtualDisplayResult) {
        if (snapshot.lastVirtualDisplayResult?.requestId == pendingVirtualDisplayRequest) pendingVirtualDisplayRequest = null
    }
    val surfaceCallback = remember {
        object : SurfaceHolder.Callback {
            override fun surfaceCreated(holder: SurfaceHolder) {
                if (holder.surface.isValid) currentSurface = holder.surface
            }

            override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) = Unit

            override fun surfaceDestroyed(holder: SurfaceHolder) {
                interpreter.cancelGesture()
                gamepadController.reset()
                if (currentSurface === holder.surface) currentSurface = null
                latestSurfaceDestroyed(holder.surface)
            }
        }
    }

    BoxWithConstraints(modifier = Modifier.fillMaxSize().background(Color.Black)) {
        val videoAspectRatio = snapshot.videoSize?.let { it.width.toFloat() / it.height } ?: (16f / 9f)
        val containerAspectRatio = if (maxHeight.value > 0f) maxWidth.value / maxHeight.value else videoAspectRatio
        val videoModifier = if (videoAspectRatio >= containerAspectRatio) {
            Modifier.fillMaxWidth().aspectRatio(videoAspectRatio)
        } else {
            Modifier.fillMaxHeight().aspectRatio(videoAspectRatio)
        }
        AndroidView(
            factory = { viewContext ->
                SurfaceView(viewContext).also { view ->
                    view.isFocusable = true
                    view.isFocusableInTouchMode = true
                    view.holder.addCallback(surfaceCallback)
                    view.setOnTouchListener { touchedView, event ->
                        if (event.isFromSource(InputDevice.SOURCE_MOUSE)) {
                            handlePhysicalMouse(event, touchedView.width, touchedView.height, latestInput)
                            return@setOnTouchListener true
                        }
                        if (latestMode == RemoteInputMode.Gamepad) return@setOnTouchListener true
                        interpreter.mode = latestMode
                        interpreter.touchpadSensitivity = latestSensitivity
                        interpreter.onTouch(event.toTouchSample(touchedView.width, touchedView.height))
                        if (event.actionMasked == MotionEvent.ACTION_DOWN) touchedView.requestFocus()
                        if (event.actionMasked == MotionEvent.ACTION_UP) touchedView.performClick()
                        true
                    }
                    view.setOnGenericMotionListener { _, event ->
                        when {
                            event.isFromSource(InputDevice.SOURCE_JOYSTICK) -> handlePhysicalGamepadMotion(event, gamepadController)
                            event.isFromSource(InputDevice.SOURCE_MOUSE) -> handlePhysicalMouse(event, view.width, view.height, latestInput)
                            else -> false
                        }
                    }
                    view.setOnKeyListener { _, keyCode, event ->
                        if (event.isFromSource(InputDevice.SOURCE_GAMEPAD) || event.isFromSource(InputDevice.SOURCE_JOYSTICK)) {
                            val button = keyCode.toRemoteGamepadButton() ?: return@setOnKeyListener false
                            if (event.repeatCount == 0) gamepadController.setButton(button, event.action == KeyEvent.ACTION_DOWN)
                            return@setOnKeyListener true
                        }
                        val key = keyCode.toRemoteKey() ?: return@setOnKeyListener false
                        when (event.action) {
                            KeyEvent.ACTION_DOWN -> if (event.repeatCount == 0) latestInput(InputCommand.Key(key, true))
                            KeyEvent.ACTION_UP -> latestInput(InputCommand.Key(key, false))
                            else -> return@setOnKeyListener false
                        }
                        return@setOnKeyListener true
                    }
                }
            },
            modifier = videoModifier.align(Alignment.Center),
            onRelease = { view ->
                interpreter.cancelGesture()
                gamepadController.reset()
                view.setOnTouchListener(null)
                view.setOnGenericMotionListener(null)
                view.setOnKeyListener(null)
                view.holder.removeCallback(surfaceCallback)
                if (currentSurface === view.holder.surface) currentSurface = null
                latestSurfaceDestroyed(view.holder.surface)
            },
        )
        RemoteTopBar(
            snapshot = snapshot,
            audioEnabled = audioEnabled,
            inputMode = inputMode,
            onInputModeChange = { mode ->
                interpreter.cancelGesture()
                if (mode != RemoteInputMode.Gamepad) gamepadController.reset()
                if (mode == RemoteInputMode.Gamepad) allowPortraitGamepad = false
                inputMode = mode
                preferences.edit().putString(INPUT_MODE, mode.name).apply()
            },
            onAudioEnabledChange = onAudioEnabledChange,
            onOpenKeyboard = { showKeyboard = true },
            onOpenDisplays = { showDisplays = true },
            onEndSession = onEndSession,
        )
        if (inputMode == RemoteInputMode.Gamepad && snapshot.status is RemoteSessionStatus.Connected) {
            if (maxWidth > maxHeight || allowPortraitGamepad) {
                RemoteGamepadOverlay(gamepadController)
            } else {
                GamepadPortraitPrompt { allowPortraitGamepad = true }
            }
        }
        RemoteStatus(snapshot.status)
    }
    if (showKeyboard) {
        RemoteKeyboardSheet(
            sensitivity = sensitivity,
            onSensitivityChange = { value ->
                sensitivity = value
                preferences.edit().putFloat(INPUT_SENSITIVITY, value).apply()
            },
            onDismiss = { showKeyboard = false },
            onInput = onInput,
            onText = onText,
            onSecureAttention = { confirmSecureAttention = true },
        )
    }
    if (confirmSecureAttention) {
        AlertDialog(
            onDismissRequest = { confirmSecureAttention = false },
            title = { Text(stringResource(R.string.remote_secure_attention)) },
            text = { Text(stringResource(R.string.remote_secure_attention_confirmation)) },
            confirmButton = {
                TextButton(
                    onClick = {
                        onInput(InputCommand.SecureAttention)
                        confirmSecureAttention = false
                    },
                ) { Text(stringResource(R.string.remote_send)) }
            },
            dismissButton = {
                TextButton(onClick = { confirmSecureAttention = false }) { Text(stringResource(R.string.remote_text_cancel)) }
            },
        )
    }
    val connected = snapshot.status as? RemoteSessionStatus.Connected
    if (showDisplays && connected != null) {
        RemoteDisplaysSheet(
            snapshot = snapshot,
            pendingRequestId = pendingVirtualDisplayRequest,
            onSwitchMonitor = onSwitchMonitor,
            onVirtualDisplayRequest = { operation ->
                val requestId = "android-${SystemClock.elapsedRealtimeNanos()}"
                pendingVirtualDisplayRequest = requestId
                onVirtualDisplayRequest(requestId, operation)
            },
            onDismiss = { showDisplays = false },
        )
    }
}

@Composable
private fun RemoteTopBar(
    snapshot: RemoteSessionSnapshot,
    audioEnabled: Boolean,
    inputMode: RemoteInputMode,
    onInputModeChange: (RemoteInputMode) -> Unit,
    onAudioEnabledChange: (Boolean) -> Unit,
    onOpenKeyboard: () -> Unit,
    onOpenDisplays: () -> Unit,
    onEndSession: () -> Unit,
) {
    val status = snapshot.status
    val title = when (status) {
        RemoteSessionStatus.Idle -> "Pixels"
        is RemoteSessionStatus.Starting -> status.request.target.displayName
        is RemoteSessionStatus.Connected -> status.request.target.displayName
        is RemoteSessionStatus.Reconnecting -> status.request.target.displayName
        is RemoteSessionStatus.Stopping -> status.request.target.displayName
        is RemoteSessionStatus.Failed -> status.request.target.displayName
    }
    MaterialSurface(color = MaterialTheme.colorScheme.surface.copy(alpha = 0.86f), modifier = Modifier.fillMaxWidth()) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 12.dp, vertical = 8.dp),
            horizontalArrangement = Arrangement.SpaceBetween,
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Column(modifier = Modifier.weight(1f)) {
                Text(text = title, style = MaterialTheme.typography.titleMedium)
                if (status is RemoteSessionStatus.Connected) {
                    Text(
                        text = stringResource(
                            R.string.remote_statistics,
                            snapshot.statistics.framesPerSecond,
                            snapshot.statistics.latencyMillis,
                            snapshot.statistics.bitrateKbps,
                        ),
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
            }
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                if ((status as? RemoteSessionStatus.Connected)?.capabilities?.supportsInput == true) {
                    FilledTonalIconButton(
                        onClick = {
                            onInputModeChange(inputMode.next())
                        },
                    ) {
                        Icon(
                            when (inputMode) {
                                RemoteInputMode.DirectTouch -> Icons.Outlined.TouchApp
                                RemoteInputMode.Touchpad -> Icons.Outlined.Mouse
                                RemoteInputMode.Gamepad -> Icons.Outlined.SportsEsports
                            },
                            contentDescription = stringResource(
                                when (inputMode) {
                                    RemoteInputMode.DirectTouch -> R.string.remote_direct_touch
                                    RemoteInputMode.Touchpad -> R.string.remote_touchpad
                                    RemoteInputMode.Gamepad -> R.string.remote_gamepad
                                },
                            ),
                        )
                    }
                    FilledTonalIconButton(onClick = onOpenKeyboard) {
                        Icon(Icons.Outlined.Keyboard, contentDescription = stringResource(R.string.remote_keyboard))
                    }
                    if (status.capabilities.monitorNames.isNotEmpty()) {
                        FilledTonalIconButton(onClick = onOpenDisplays) {
                            Icon(Icons.Outlined.DesktopWindows, contentDescription = stringResource(R.string.remote_displays))
                        }
                    }
                }
                if ((status as? RemoteSessionStatus.Connected)?.capabilities?.supportsAudio == true) {
                    FilledTonalIconButton(onClick = { onAudioEnabledChange(!audioEnabled) }) {
                        Icon(
                            if (audioEnabled) Icons.AutoMirrored.Outlined.VolumeUp else Icons.AutoMirrored.Outlined.VolumeOff,
                            contentDescription = stringResource(if (audioEnabled) R.string.remote_mute else R.string.remote_unmute),
                        )
                    }
                }
                FilledTonalIconButton(onClick = onEndSession) {
                    Icon(Icons.Outlined.Close, contentDescription = stringResource(R.string.remote_exit))
                }
            }
        }
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RemoteDisplaysSheet(
    snapshot: RemoteSessionSnapshot,
    pendingRequestId: String?,
    onSwitchMonitor: (String) -> Unit,
    onVirtualDisplayRequest: (RemoteVirtualDisplayOperation) -> Unit,
    onDismiss: () -> Unit,
) {
    val connected = snapshot.status as? RemoteSessionStatus.Connected ?: return
    val capabilities = connected.capabilities
    val monitorNames = capabilities.monitorNames
    val activeMonitorName = capabilities.activeMonitorName
    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).padding(bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(stringResource(R.string.remote_displays), style = MaterialTheme.typography.titleLarge)
            if (monitorNames.isEmpty()) {
                Text(stringResource(R.string.remote_no_displays), color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            monitorNames.forEachIndexed { index, monitorName ->
                val label = stringResource(R.string.remote_display_name, index + 1, monitorName)
                if (monitorName == activeMonitorName) {
                    FilledTonalButton(onClick = {}, enabled = false, modifier = Modifier.fillMaxWidth()) { Text(label) }
                } else {
                    OutlinedButton(
                        onClick = {
                            onSwitchMonitor(monitorName)
                            onDismiss()
                        },
                        modifier = Modifier.fillMaxWidth(),
                    ) { Text(label) }
                }
            }
            if (capabilities.supportsVirtualDisplays) {
                Text(
                    stringResource(
                        R.string.remote_virtual_display_count,
                        capabilities.ownedVirtualDisplayCount,
                        capabilities.maximumVirtualDisplayCount,
                    ),
                    style = MaterialTheme.typography.titleMedium,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    FilledTonalButton(
                        onClick = { onVirtualDisplayRequest(RemoteVirtualDisplayOperation.Create) },
                        enabled = pendingRequestId == null &&
                            capabilities.ownedVirtualDisplayCount < capabilities.maximumVirtualDisplayCount,
                    ) { Text(stringResource(R.string.remote_virtual_display_add)) }
                    OutlinedButton(
                        onClick = { onVirtualDisplayRequest(RemoteVirtualDisplayOperation.RemoveLast) },
                        enabled = pendingRequestId == null && capabilities.ownedVirtualDisplayCount > 0,
                    ) { Text(stringResource(R.string.remote_virtual_display_remove)) }
                }
                if (pendingRequestId != null) {
                    Text(stringResource(R.string.remote_virtual_display_processing), color = MaterialTheme.colorScheme.onSurfaceVariant)
                }
                snapshot.lastVirtualDisplayResult?.takeIf { !it.accepted }?.let { result ->
                    Text(
                        result.errorMessage.ifBlank { result.errorCode.ifBlank { stringResource(R.string.remote_virtual_display_failed) } },
                        color = MaterialTheme.colorScheme.error,
                    )
                }
            }
        }
    }
}

private enum class ModifierMode { Off, OneShot, Locked }

@OptIn(ExperimentalMaterial3Api::class)
@Composable
private fun RemoteKeyboardSheet(
    sensitivity: Float,
    onSensitivityChange: (Float) -> Unit,
    onDismiss: () -> Unit,
    onInput: (InputCommand) -> Unit,
    onText: (String) -> Unit,
    onSecureAttention: () -> Unit,
) {
    var text by remember { mutableStateOf("") }
    val modifiers = remember { mutableStateMapOf<RemoteKey, ModifierMode>() }
    val activeModifiers = listOf(RemoteKey.Control, RemoteKey.Alt, RemoteKey.Shift, RemoteKey.Meta)
    fun sendKey(key: RemoteKey) {
        val selected = activeModifiers.filter { modifiers[it] != null && modifiers[it] != ModifierMode.Off }
        selected.forEach { onInput(InputCommand.Key(it, true)) }
        onInput(InputCommand.Key(key, true))
        onInput(InputCommand.Key(key, false))
        selected.asReversed().forEach { onInput(InputCommand.Key(it, false)) }
        selected.filter { modifiers[it] == ModifierMode.OneShot }.forEach { modifiers[it] = ModifierMode.Off }
    }
    fun cycleModifier(key: RemoteKey) {
        modifiers[key] = when (modifiers[key] ?: ModifierMode.Off) {
            ModifierMode.Off -> ModifierMode.OneShot
            ModifierMode.OneShot -> ModifierMode.Locked
            ModifierMode.Locked -> ModifierMode.Off
        }
    }

    ModalBottomSheet(onDismissRequest = onDismiss) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 16.dp).padding(bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            OutlinedTextField(
                value = text,
                onValueChange = { candidate -> if (candidate.encodeToByteArray().size <= 4096) text = candidate },
                modifier = Modifier.fillMaxWidth(),
                placeholder = { Text(stringResource(R.string.remote_text_hint)) },
                maxLines = 3,
                trailingIcon = {
                    TextButton(
                        onClick = {
                            onText(text)
                            text = ""
                        },
                        enabled = text.isNotEmpty(),
                    ) { Text(stringResource(R.string.remote_text_send)) }
                },
            )
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                KeyButton("Esc") { sendKey(RemoteKey.Escape) }
                KeyButton("Tab") { sendKey(RemoteKey.Tab) }
                ModifierButton("Ctrl", modifiers[RemoteKey.Control] ?: ModifierMode.Off) { cycleModifier(RemoteKey.Control) }
                ModifierButton("Alt", modifiers[RemoteKey.Alt] ?: ModifierMode.Off) { cycleModifier(RemoteKey.Alt) }
                ModifierButton("Shift", modifiers[RemoteKey.Shift] ?: ModifierMode.Off) { cycleModifier(RemoteKey.Shift) }
                ModifierButton("Win", modifiers[RemoteKey.Meta] ?: ModifierMode.Off) { cycleModifier(RemoteKey.Meta) }
                KeyButton("←") { sendKey(RemoteKey.ArrowLeft) }
                KeyButton("↑") { sendKey(RemoteKey.ArrowUp) }
                KeyButton("↓") { sendKey(RemoteKey.ArrowDown) }
                KeyButton("→") { sendKey(RemoteKey.ArrowRight) }
                KeyButton("Del") { sendKey(RemoteKey.Delete) }
            }
            Row(
                modifier = Modifier.fillMaxWidth().horizontalScroll(rememberScrollState()),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                ShortcutButton(stringResource(R.string.remote_shortcut_copy)) { sendChord(listOf(RemoteKey.Control), RemoteKey.C, onInput) }
                ShortcutButton(stringResource(R.string.remote_shortcut_paste)) { sendChord(listOf(RemoteKey.Control), RemoteKey.V, onInput) }
                ShortcutButton(stringResource(R.string.remote_shortcut_select_all)) { sendChord(listOf(RemoteKey.Control), RemoteKey.A, onInput) }
                ShortcutButton(stringResource(R.string.remote_shortcut_undo)) { sendChord(listOf(RemoteKey.Control), RemoteKey.Z, onInput) }
                ShortcutButton(stringResource(R.string.remote_shortcut_task_manager)) {
                    sendChord(listOf(RemoteKey.Control, RemoteKey.Shift), RemoteKey.Escape, onInput)
                }
                ShortcutButton(stringResource(R.string.remote_shortcut_desktop)) { sendChord(listOf(RemoteKey.Meta), RemoteKey.D, onInput) }
                OutlinedButton(onClick = onSecureAttention) { Text("Ctrl+Alt+Delete") }
            }
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.remote_touchpad_speed), modifier = Modifier.padding(end = 12.dp))
                Slider(value = sensitivity, onValueChange = onSensitivityChange, valueRange = 0.5f..2f)
            }
        }
    }
}

@Composable
private fun KeyButton(label: String, onClick: () -> Unit) {
    OutlinedButton(onClick = onClick) { Text(label) }
}

@Composable
private fun ShortcutButton(label: String, onClick: () -> Unit) {
    FilledTonalButton(onClick = onClick) { Text(label) }
}

@Composable
private fun ModifierButton(label: String, mode: ModifierMode, onClick: () -> Unit) {
    if (mode == ModifierMode.Off) {
        OutlinedButton(onClick = onClick) { Text(label) }
    } else {
        Button(onClick = onClick) { Text(if (mode == ModifierMode.Locked) "$label 🔒" else "$label ·") }
    }
}

private fun sendChord(modifiers: List<RemoteKey>, key: RemoteKey, onInput: (InputCommand) -> Unit) {
    modifiers.forEach { onInput(InputCommand.Key(it, true)) }
    onInput(InputCommand.Key(key, true))
    onInput(InputCommand.Key(key, false))
    modifiers.toList().asReversed().forEach { onInput(InputCommand.Key(it, false)) }
}

private fun handlePhysicalMouse(event: MotionEvent, width: Int, height: Int, onInput: (InputCommand) -> Unit): Boolean {
    if (width <= 0 || height <= 0) return false
    val xRatio = (event.x / width).coerceIn(0f, 1f)
    val yRatio = (event.y / height).coerceIn(0f, 1f)
    when (event.actionMasked) {
        MotionEvent.ACTION_HOVER_MOVE, MotionEvent.ACTION_MOVE -> onInput(InputCommand.MoveAbsolute(xRatio, yRatio))
        MotionEvent.ACTION_SCROLL -> {
            val deltaX = (event.getAxisValue(MotionEvent.AXIS_HSCROLL) * 120).toInt()
            val deltaY = (event.getAxisValue(MotionEvent.AXIS_VSCROLL) * 120).toInt()
            if (deltaX != 0 || deltaY != 0) onInput(InputCommand.Wheel(deltaX, deltaY))
        }
        MotionEvent.ACTION_BUTTON_PRESS, MotionEvent.ACTION_DOWN -> onInput(
            InputCommand.MouseButton(event.actionButton.toRemoteMouseButton(), true, xRatio, yRatio),
        )
        MotionEvent.ACTION_BUTTON_RELEASE, MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> onInput(
            InputCommand.MouseButton(event.actionButton.toRemoteMouseButton(), false, xRatio, yRatio),
        )
        else -> return false
    }
    return true
}

private fun handlePhysicalGamepadMotion(event: MotionEvent, controller: RemoteGamepadController): Boolean {
    if (event.actionMasked != MotionEvent.ACTION_MOVE) return false
    controller.setLeftStick(event.getCenteredAxis(MotionEvent.AXIS_X), event.getCenteredAxis(MotionEvent.AXIS_Y))
    controller.setRightStick(event.getCenteredAxis(MotionEvent.AXIS_Z), event.getCenteredAxis(MotionEvent.AXIS_RZ))
    val leftTrigger = maxOf(event.getNormalizedTrigger(MotionEvent.AXIS_LTRIGGER), event.getNormalizedTrigger(MotionEvent.AXIS_BRAKE))
    val rightTrigger = maxOf(event.getNormalizedTrigger(MotionEvent.AXIS_RTRIGGER), event.getNormalizedTrigger(MotionEvent.AXIS_GAS))
    controller.setTriggers(leftTrigger, rightTrigger)
    controller.setButton(RemoteGamepadButton.DPadLeft, event.getCenteredAxis(MotionEvent.AXIS_HAT_X) < -0.5f)
    controller.setButton(RemoteGamepadButton.DPadRight, event.getCenteredAxis(MotionEvent.AXIS_HAT_X) > 0.5f)
    controller.setButton(RemoteGamepadButton.DPadUp, event.getCenteredAxis(MotionEvent.AXIS_HAT_Y) < -0.5f)
    controller.setButton(RemoteGamepadButton.DPadDown, event.getCenteredAxis(MotionEvent.AXIS_HAT_Y) > 0.5f)
    return true
}

private fun MotionEvent.getCenteredAxis(axis: Int): Float {
    val range = device?.getMotionRange(axis, source) ?: return 0f
    val value = getAxisValue(axis)
    val center = (range.min + range.max) / 2f
    val halfRange = (range.max - range.min) / 2f
    if (halfRange <= 0f || kotlin.math.abs(value - center) <= range.flat) return 0f
    return ((value - center) / halfRange).coerceIn(-1f, 1f)
}

private fun MotionEvent.getNormalizedTrigger(axis: Int): Float {
    val range = device?.getMotionRange(axis, source) ?: return 0f
    if (range.range <= 0f) return 0f
    return ((getAxisValue(axis) - range.min) / range.range).coerceIn(0f, 1f)
}

private fun Int.toRemoteGamepadButton(): RemoteGamepadButton? = when (this) {
    KeyEvent.KEYCODE_BUTTON_A -> RemoteGamepadButton.A
    KeyEvent.KEYCODE_BUTTON_B -> RemoteGamepadButton.B
    KeyEvent.KEYCODE_BUTTON_X -> RemoteGamepadButton.X
    KeyEvent.KEYCODE_BUTTON_Y -> RemoteGamepadButton.Y
    KeyEvent.KEYCODE_BUTTON_L1 -> RemoteGamepadButton.LeftShoulder
    KeyEvent.KEYCODE_BUTTON_R1 -> RemoteGamepadButton.RightShoulder
    KeyEvent.KEYCODE_BUTTON_THUMBL -> RemoteGamepadButton.LeftThumb
    KeyEvent.KEYCODE_BUTTON_THUMBR -> RemoteGamepadButton.RightThumb
    KeyEvent.KEYCODE_BUTTON_START -> RemoteGamepadButton.Start
    KeyEvent.KEYCODE_BUTTON_SELECT, KeyEvent.KEYCODE_BACK -> RemoteGamepadButton.Back
    KeyEvent.KEYCODE_DPAD_UP -> RemoteGamepadButton.DPadUp
    KeyEvent.KEYCODE_DPAD_DOWN -> RemoteGamepadButton.DPadDown
    KeyEvent.KEYCODE_DPAD_LEFT -> RemoteGamepadButton.DPadLeft
    KeyEvent.KEYCODE_DPAD_RIGHT -> RemoteGamepadButton.DPadRight
    else -> null
}

private fun RemoteInputMode.next(): RemoteInputMode = when (this) {
    RemoteInputMode.DirectTouch -> RemoteInputMode.Touchpad
    RemoteInputMode.Touchpad -> RemoteInputMode.Gamepad
    RemoteInputMode.Gamepad -> RemoteInputMode.DirectTouch
}

private fun Int.toRemoteMouseButton(): RemoteMouseButton = when (this) {
    MotionEvent.BUTTON_SECONDARY -> RemoteMouseButton.Right
    MotionEvent.BUTTON_TERTIARY -> RemoteMouseButton.Middle
    else -> RemoteMouseButton.Left
}

private fun MotionEvent.toTouchSample(width: Int, height: Int): TouchSample {
    val action = when (actionMasked) {
        MotionEvent.ACTION_DOWN -> TouchAction.Down
        MotionEvent.ACTION_POINTER_DOWN -> TouchAction.PointerDown
        MotionEvent.ACTION_MOVE -> TouchAction.Move
        MotionEvent.ACTION_POINTER_UP -> TouchAction.PointerUp
        MotionEvent.ACTION_UP -> TouchAction.Up
        else -> TouchAction.Cancel
    }
    return TouchSample(
        action = action,
        points = List(pointerCount) { index -> TouchPoint(getX(index), getY(index)) },
        eventTimeMillis = eventTime,
        width = width,
        height = height,
    )
}

@Composable
private fun BoxScope.RemoteStatus(status: RemoteSessionStatus) {
    val message = when (status) {
        RemoteSessionStatus.Idle -> R.string.remote_connecting
        is RemoteSessionStatus.Starting -> R.string.remote_connecting
        is RemoteSessionStatus.Reconnecting -> R.string.remote_reconnecting
        is RemoteSessionStatus.Stopping -> R.string.remote_stopping
        is RemoteSessionStatus.Failed -> R.string.remote_failed
        is RemoteSessionStatus.Connected -> null
    }
    if (message != null) {
        Column(
            modifier = Modifier.align(Alignment.Center),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(16.dp),
        ) {
            if (status !is RemoteSessionStatus.Failed) CircularProgressIndicator()
            Text(text = stringResource(message), color = Color.White)
        }
    }
}

private const val INPUT_PREFERENCES = "remote_input"
private const val INPUT_MODE = "mode"
private const val INPUT_SENSITIVITY = "sensitivity"
