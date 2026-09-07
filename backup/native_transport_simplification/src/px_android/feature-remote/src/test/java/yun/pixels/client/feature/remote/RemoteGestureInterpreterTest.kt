package yun.pixels.client.feature.remote

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteMouseButton

class RemoteGestureInterpreterTest {
    @Test
    fun directTapProducesPositionedLeftClick() {
        val commands = mutableListOf<InputCommand>()
        val interpreter = RemoteGestureInterpreter(commands::add)

        interpreter.onTouch(sample(TouchAction.Down, 10, point(25f, 75f)))
        interpreter.onTouch(sample(TouchAction.Up, 80, point(25f, 75f)))

        assertEquals(
            listOf(
                InputCommand.MouseButton(RemoteMouseButton.Left, true, 0.25f, 0.75f),
                InputCommand.MouseButton(RemoteMouseButton.Left, false, 0.25f, 0.75f),
            ),
            commands,
        )
    }

    @Test
    fun touchpadMovementIsRelativeAndUsesSensitivity() {
        val commands = mutableListOf<InputCommand>()
        val interpreter = RemoteGestureInterpreter(commands::add).apply {
            mode = RemoteInputMode.Touchpad
            touchpadSensitivity = 2f
        }

        interpreter.onTouch(sample(TouchAction.Down, 0, point(10f, 20f)))
        interpreter.onTouch(sample(TouchAction.Move, 20, point(20f, 30f)))

        assertEquals(InputCommand.MoveRelative(0.2f, 0.2f), commands.single())
    }

    @Test
    fun twoFingerTapProducesRightClickAndMovementProducesWheel() {
        val tapCommands = mutableListOf<InputCommand>()
        val tap = RemoteGestureInterpreter(tapCommands::add)
        tap.onTouch(sample(TouchAction.Down, 0, point(20f, 20f)))
        tap.onTouch(sample(TouchAction.PointerDown, 10, point(20f, 20f), point(40f, 20f)))
        tap.onTouch(sample(TouchAction.Up, 80, point(30f, 20f)))
        assertEquals(RemoteMouseButton.Right, (tapCommands.first() as InputCommand.MouseButton).button)

        val wheelCommands = mutableListOf<InputCommand>()
        val wheel = RemoteGestureInterpreter(wheelCommands::add)
        wheel.onTouch(sample(TouchAction.Down, 0, point(20f, 20f)))
        wheel.onTouch(sample(TouchAction.PointerDown, 10, point(20f, 20f), point(40f, 20f)))
        wheel.onTouch(sample(TouchAction.Move, 20, point(20f, 40f), point(40f, 40f)))
        assertTrue(wheelCommands.single() is InputCommand.Wheel)
    }

    @Test
    fun cancelReleasesAnActiveDrag() {
        val commands = mutableListOf<InputCommand>()
        val interpreter = RemoteGestureInterpreter(commands::add)
        interpreter.onTouch(sample(TouchAction.Down, 0, point(20f, 20f)))
        interpreter.onTouch(sample(TouchAction.Move, 500, point(30f, 30f)))
        interpreter.onTouch(sample(TouchAction.Cancel, 510, point(30f, 30f)))

        assertEquals(InputCommand.MouseButton(RemoteMouseButton.Left, false), commands.last())
    }

    private fun sample(action: TouchAction, time: Long, vararg points: TouchPoint) = TouchSample(
        action = action,
        points = points.toList(),
        eventTimeMillis = time,
        width = 100,
        height = 100,
    )

    private fun point(x: Float, y: Float) = TouchPoint(x, y)
}
