package yun.pixels.client.feature.devices

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Switch
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteSessionPreferences

@Composable
fun DeviceSessionPreferencesDialog(
    displayName: String,
    preferences: RemoteSessionPreferences?,
    isSaving: Boolean,
    onDismiss: () -> Unit,
    onSave: (RemoteSessionPreferences) -> Unit,
) {
    var draft by remember(preferences) { mutableStateOf(preferences ?: RemoteSessionPreferences()) }
    AlertDialog(
        onDismissRequest = { if (!isSaving) onDismiss() },
        title = { Text(stringResource(R.string.connection_preferences_title, displayName)) },
        text = {
            if (preferences == null) {
                Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.Center) { CircularProgressIndicator() }
            } else {
                Column(verticalArrangement = Arrangement.spacedBy(18.dp)) {
                    PreferenceSection(title = stringResource(R.string.preferred_frame_rate)) {
                        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                            RemoteSessionPreferences.SUPPORTED_FRAME_RATES.sorted().forEach { frameRate ->
                                FilterChip(
                                    selected = draft.frameRate == frameRate,
                                    onClick = { draft = draft.copy(frameRate = frameRate) },
                                    label = { Text(stringResource(R.string.frame_rate_value, frameRate)) },
                                )
                            }
                        }
                    }
                    Row(
                        modifier = Modifier.fillMaxWidth(),
                        horizontalArrangement = Arrangement.SpaceBetween,
                        verticalAlignment = Alignment.CenterVertically,
                    ) {
                        Column(modifier = Modifier.weight(1f)) {
                            Text(stringResource(R.string.play_remote_audio))
                            Text(stringResource(R.string.play_remote_audio_description))
                        }
                        Switch(checked = draft.audioEnabled, onCheckedChange = { draft = draft.copy(audioEnabled = it) })
                    }
                    PreferenceSection(title = stringResource(R.string.default_input_mode)) {
                        Column(verticalArrangement = Arrangement.spacedBy(6.dp)) {
                            RemoteInputMode.entries.forEach { mode ->
                                FilterChip(
                                    selected = draft.inputMode == mode,
                                    onClick = { draft = draft.copy(inputMode = mode) },
                                    label = { Text(stringResource(mode.labelResource())) },
                                )
                            }
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { onSave(draft) }, enabled = preferences != null && !isSaving) {
                Text(stringResource(if (isSaving) R.string.saving else R.string.save))
            }
        },
        dismissButton = { TextButton(onClick = onDismiss, enabled = !isSaving) { Text(stringResource(R.string.cancel)) } },
    )
}

@Composable
private fun PreferenceSection(title: String, content: @Composable () -> Unit) {
    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text(title)
        content()
    }
}

private fun RemoteInputMode.labelResource(): Int = when (this) {
    RemoteInputMode.DirectTouch -> R.string.input_mode_direct_touch
    RemoteInputMode.Touchpad -> R.string.input_mode_touchpad
    RemoteInputMode.Gamepad -> R.string.input_mode_gamepad
}
