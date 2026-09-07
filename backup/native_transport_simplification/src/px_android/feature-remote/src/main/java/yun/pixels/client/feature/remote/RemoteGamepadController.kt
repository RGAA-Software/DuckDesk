package yun.pixels.client.feature.remote

import kotlin.math.abs
import kotlin.math.roundToInt
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteGamepadButton
import yun.pixels.client.core.domain.session.RemoteGamepadState

internal class RemoteGamepadController(
    private val emit: (InputCommand) -> Unit,
) {
    var configuration: GamepadConfiguration = GamepadConfiguration()
    var state: RemoteGamepadState = RemoteGamepadState()
        private set

    fun setButton(button: RemoteGamepadButton, down: Boolean) {
        val buttons = if (down) state.buttons or button.mask else state.buttons and button.mask.inv()
        update(state.copy(buttons = buttons and 0xFFFF))
    }

    fun setLeftStick(x: Float, y: Float) {
        update(state.copy(leftThumbX = axis(x), leftThumbY = axis(-y)))
    }

    fun setRightStick(x: Float, y: Float) {
        update(state.copy(rightThumbX = axis(x), rightThumbY = axis(-y)))
    }

    fun setTriggers(left: Float, right: Float) {
        update(state.copy(leftTrigger = trigger(left), rightTrigger = trigger(right)))
    }

    fun reset() {
        update(RemoteGamepadState())
    }

    private fun update(next: RemoteGamepadState) {
        if (next == state) return
        state = next
        emit(InputCommand.Gamepad(next))
    }

    private fun axis(value: Float): Int {
        val normalized = value.takeIf(Float::isFinite)?.coerceIn(-1f, 1f) ?: 0f
        val config = configuration.normalized()
        if (abs(normalized) < config.stickDeadZone) return 0
        val adjusted = (normalized * config.stickSensitivity).coerceIn(-1f, 1f)
        return (adjusted * Short.MAX_VALUE).roundToInt().coerceIn(Short.MIN_VALUE.toInt(), Short.MAX_VALUE.toInt())
    }

    private fun trigger(value: Float): Int {
        val normalized = value.takeIf(Float::isFinite)?.coerceIn(0f, 1f) ?: 0f
        if (normalized < TRIGGER_DEAD_ZONE) return 0
        return (normalized * 0xFF).roundToInt().coerceIn(0, 0xFF)
    }

    private companion object {
        const val TRIGGER_DEAD_ZONE = 0.04f
    }
}
