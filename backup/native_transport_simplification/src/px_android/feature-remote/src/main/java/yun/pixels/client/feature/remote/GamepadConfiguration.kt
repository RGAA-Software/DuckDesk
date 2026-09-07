package yun.pixels.client.feature.remote

internal enum class VirtualGamepadLayout {
    Standard,
    Southpaw,
}

internal data class GamepadConfiguration(
    val layout: VirtualGamepadLayout = VirtualGamepadLayout.Standard,
    val stickDeadZone: Float = 0.08f,
    val stickSensitivity: Float = 1f,
    val overlayOpacity: Float = 0.72f,
    val overlayScale: Float = 1f,
) {
    fun normalized(): GamepadConfiguration = copy(
        stickDeadZone = stickDeadZone.coerceIn(0f, 0.35f),
        stickSensitivity = stickSensitivity.coerceIn(0.5f, 1.5f),
        overlayOpacity = overlayOpacity.coerceIn(0.4f, 1f),
        overlayScale = overlayScale.coerceIn(0.8f, 1.2f),
    )
}
