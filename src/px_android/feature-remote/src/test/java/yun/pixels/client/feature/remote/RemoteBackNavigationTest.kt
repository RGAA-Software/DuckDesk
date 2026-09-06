package yun.pixels.client.feature.remote

import org.junit.Assert.assertEquals
import org.junit.Test
import yun.pixels.client.core.domain.session.RemoteInputMode

class RemoteBackNavigationTest {
    @Test
    fun `back closes only the highest priority remote layer`() {
        assertEquals(
            RemoteBackAction.DismissSecureAttention,
            action(secureAttention = true, keyboard = true, gamepadSettings = true, displays = true),
        )
        assertEquals(
            RemoteBackAction.DismissKeyboard,
            action(keyboard = true, gamepadSettings = true, displays = true),
        )
        assertEquals(
            RemoteBackAction.DismissGamepadSettings,
            action(gamepadSettings = true, displays = true),
        )
        assertEquals(RemoteBackAction.DismissDisplays, action(displays = true))
    }

    @Test
    fun `back exits gamepad before requesting session end`() {
        assertEquals(RemoteBackAction.ExitGamepadMode, action(inputMode = RemoteInputMode.Gamepad))
        assertEquals(RemoteBackAction.RequestSessionEnd, action(inputMode = RemoteInputMode.DirectTouch))
        assertEquals(RemoteBackAction.RequestSessionEnd, action(inputMode = RemoteInputMode.Touchpad))
    }

    private fun action(
        secureAttention: Boolean = false,
        keyboard: Boolean = false,
        gamepadSettings: Boolean = false,
        displays: Boolean = false,
        inputMode: RemoteInputMode = RemoteInputMode.DirectTouch,
    ): RemoteBackAction = resolveRemoteBackAction(
        confirmSecureAttention = secureAttention,
        showKeyboard = keyboard,
        showGamepadSettings = gamepadSettings,
        showDisplays = displays,
        inputMode = inputMode,
    )
}
