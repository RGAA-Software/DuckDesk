package yun.pixels.client.feature.remote

import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.awaitEachGesture
import androidx.compose.foundation.gestures.awaitFirstDown
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.gestures.waitForUpOrCancellation
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.onClick
import androidx.compose.ui.semantics.role
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.session.RemoteGamepadButton

@Composable
internal fun BoxScope.RemoteGamepadOverlay(controller: RemoteGamepadController) {
    Row(
        modifier = Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(horizontal = 12.dp, vertical = 18.dp),
        horizontalArrangement = Arrangement.SpaceBetween,
        verticalAlignment = Alignment.Bottom,
    ) {
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            GamepadButton("LB", RemoteGamepadButton.LeftShoulder, controller)
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                VirtualStick("L", controller::setLeftStick)
                DirectionButtons(controller)
            }
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                GamepadButton("Back", RemoteGamepadButton.Back, controller, compact = true)
                GamepadButton("Start", RemoteGamepadButton.Start, controller, compact = true)
            }
            Text("Pixels Gamepad", color = Color.White.copy(alpha = 0.72f), style = MaterialTheme.typography.labelSmall)
        }
        Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(8.dp)) {
            GamepadButton("RB", RemoteGamepadButton.RightShoulder, controller)
            Row(horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                VirtualStick("R", controller::setRightStick)
                ActionButtons(controller)
            }
        }
    }
}

@Composable
internal fun BoxScope.GamepadPortraitPrompt(onContinue: () -> Unit) {
    Column(
        modifier = Modifier
            .align(Alignment.BottomCenter)
            .padding(20.dp)
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.9f), RoundedCornerShape(20.dp))
            .padding(20.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        Text(stringResource(R.string.remote_gamepad_rotate), color = MaterialTheme.colorScheme.onSurface)
        Button(onClick = onContinue) { Text(stringResource(R.string.remote_gamepad_continue_portrait)) }
    }
}

@Composable
private fun DirectionButtons(controller: RemoteGamepadController) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        GamepadButton("↑", RemoteGamepadButton.DPadUp, controller)
        Row {
            GamepadButton("←", RemoteGamepadButton.DPadLeft, controller)
            Box(Modifier.size(44.dp))
            GamepadButton("→", RemoteGamepadButton.DPadRight, controller)
        }
        GamepadButton("↓", RemoteGamepadButton.DPadDown, controller)
    }
}

@Composable
private fun ActionButtons(controller: RemoteGamepadController) {
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        GamepadButton("Y", RemoteGamepadButton.Y, controller)
        Row {
            GamepadButton("X", RemoteGamepadButton.X, controller)
            Box(Modifier.size(44.dp))
            GamepadButton("B", RemoteGamepadButton.B, controller)
        }
        GamepadButton("A", RemoteGamepadButton.A, controller)
    }
}

@Composable
private fun GamepadButton(
    label: String,
    button: RemoteGamepadButton,
    controller: RemoteGamepadController,
    compact: Boolean = false,
) {
    Box(
        modifier = Modifier
            .size(if (compact) 48.dp else 44.dp)
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.72f), CircleShape)
            .semantics {
                role = Role.Button
                contentDescription = label
                onClick {
                    controller.setButton(button, true)
                    controller.setButton(button, false)
                    true
                }
            }
            .pointerInput(button) {
                awaitEachGesture {
                    awaitFirstDown(requireUnconsumed = false)
                    controller.setButton(button, true)
                    waitForUpOrCancellation()
                    controller.setButton(button, false)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = Color.White, style = MaterialTheme.typography.labelMedium)
    }
}

@Composable
private fun VirtualStick(label: String, onPosition: (Float, Float) -> Unit) {
    Box(
        modifier = Modifier
            .size(104.dp)
            .background(MaterialTheme.colorScheme.surface.copy(alpha = 0.55f), CircleShape)
            .semantics { contentDescription = "$label stick" }
            .pointerInput(label) {
                fun update(position: Offset) {
                    val radius = minOf(size.width, size.height) / 2f
                    onPosition(((position.x - radius) / radius).coerceIn(-1f, 1f), ((position.y - radius) / radius).coerceIn(-1f, 1f))
                }
                detectDragGestures(
                    onDragStart = ::update,
                    onDragEnd = { onPosition(0f, 0f) },
                    onDragCancel = { onPosition(0f, 0f) },
                ) { change, _ ->
                    change.consume()
                    update(change.position)
                }
            },
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = Color.White.copy(alpha = 0.8f), style = MaterialTheme.typography.titleMedium)
    }
}
