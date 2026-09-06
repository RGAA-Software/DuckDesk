package yun.pixels.client.feature.remote

import kotlin.math.roundToInt
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteGamepadButton
import yun.pixels.client.core.domain.session.RemoteGamepadState

class RemoteGamepadControllerTest {
    @Test
    fun buttonTransitionsPreserveOtherButtonsAndReleaseCleanly() {
        val commands = mutableListOf<InputCommand>()
        val controller = RemoteGamepadController(commands::add)

        controller.setButton(RemoteGamepadButton.A, true)
        controller.setButton(RemoteGamepadButton.RightShoulder, true)
        controller.setButton(RemoteGamepadButton.A, false)

        val state = (commands.last() as InputCommand.Gamepad).state
        assertEquals(RemoteGamepadButton.RightShoulder.mask, state.buttons)
    }

    @Test
    fun axesApplyDeadZonesClampingAndWindowsYAxisDirection() {
        val commands = mutableListOf<InputCommand>()
        val controller = RemoteGamepadController(commands::add)

        controller.setLeftStick(0.01f, -2f)
        controller.setRightStick(0.5f, 0.5f)
        controller.setTriggers(0.01f, 2f)

        assertEquals(0, controller.state.leftThumbX)
        assertEquals(Short.MAX_VALUE.toInt(), controller.state.leftThumbY)
        assertTrue(controller.state.rightThumbX > 0)
        assertTrue(controller.state.rightThumbY < 0)
        assertEquals(0, controller.state.leftTrigger)
        assertEquals(0xFF, controller.state.rightTrigger)
    }

    @Test
    fun resetEmitsNeutralStateOnlyWhenNeeded() {
        val commands = mutableListOf<InputCommand>()
        val controller = RemoteGamepadController(commands::add)
        controller.setButton(RemoteGamepadButton.Start, true)
        controller.reset()
        controller.reset()

        assertEquals(2, commands.size)
        assertEquals(RemoteGamepadState(), (commands.last() as InputCommand.Gamepad).state)
    }

    @Test
    fun savedDeadZoneAndSensitivityAreAppliedToStickInput() {
        val commands = mutableListOf<InputCommand>()
        val controller = RemoteGamepadController(commands::add)
        controller.configuration = GamepadConfiguration(stickDeadZone = 0.2f, stickSensitivity = 1.5f)

        controller.setLeftStick(0.1f, 0f)
        assertEquals(0, controller.state.leftThumbX)

        controller.setLeftStick(0.5f, 0f)
        assertEquals((Short.MAX_VALUE * 0.75f).roundToInt(), controller.state.leftThumbX)
    }
}
