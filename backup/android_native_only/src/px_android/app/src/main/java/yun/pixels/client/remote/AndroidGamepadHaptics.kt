package yun.pixels.client.remote

import android.content.Context
import android.hardware.input.InputManager
import android.os.CombinedVibration
import android.os.VibrationEffect
import android.os.VibratorManager
import android.view.InputDevice

internal class AndroidGamepadHaptics(context: Context) {
    private val inputManager = context.getSystemService(InputManager::class.java)
    private val deviceVibratorManager = context.getSystemService(VibratorManager::class.java)
    private var activeManagers: List<VibratorManager> = emptyList()

    fun apply(strongMotor: Int, weakMotor: Int) {
        stop()
        val strong = strongMotor.coerceIn(0, MAX_AMPLITUDE)
        val weak = weakMotor.coerceIn(0, MAX_AMPLITUDE)
        if (strong == 0 && weak == 0) return
        val controllerManagers = inputManager.inputDeviceIds.asSequence()
            .mapNotNull(inputManager::getInputDevice)
            .filter(::isGamepad)
            .map(InputDevice::getVibratorManager)
            .filter(::hasVibrators)
            .toList()
        val targets = controllerManagers.ifEmpty { listOf(deviceVibratorManager).filter(::hasVibrators) }
        targets.forEach { manager -> manager.vibrate(createVibration(manager, strong, weak, manager in controllerManagers)) }
        activeManagers = targets
    }

    fun stop() {
        activeManagers.forEach(VibratorManager::cancel)
        activeManagers = emptyList()
    }

    private fun createVibration(manager: VibratorManager, strong: Int, weak: Int, splitMotors: Boolean): CombinedVibration {
        val vibratorIds = manager.vibratorIds.filter { manager.getVibrator(it).hasVibrator() }
        val parallel = CombinedVibration.startParallel()
        if (splitMotors && vibratorIds.size >= 2) {
            if (strong > 0) parallel.addVibrator(vibratorIds[0], createEffect(strong))
            if (weak > 0) parallel.addVibrator(vibratorIds[1], createEffect(weak))
        } else {
            val amplitude = maxOf(strong, weak)
            vibratorIds.forEach { vibratorId -> parallel.addVibrator(vibratorId, createEffect(amplitude)) }
        }
        return parallel.combine()
    }

    private fun createEffect(amplitude: Int): VibrationEffect = VibrationEffect.createWaveform(
        longArrayOf(0L, PULSE_WINDOW_MILLIS),
        intArrayOf(0, amplitude),
        REPEAT_INDEX,
    )

    private fun isGamepad(device: InputDevice): Boolean {
        val sources = device.sources
        return (sources and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
            (sources and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }

    private fun hasVibrators(manager: VibratorManager): Boolean =
        manager.vibratorIds.any { vibratorId -> manager.getVibrator(vibratorId).hasVibrator() }

    private companion object {
        const val MAX_AMPLITUDE = 255
        const val PULSE_WINDOW_MILLIS = 1_000L
        const val REPEAT_INDEX = 0
    }
}
