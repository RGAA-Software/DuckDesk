package yun.pixels.client.feature.remote

import kotlin.math.abs
import kotlin.math.roundToInt
import yun.pixels.client.core.domain.session.InputCommand
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteMouseButton

internal enum class TouchAction {
    Down,
    PointerDown,
    Move,
    PointerUp,
    Up,
    Cancel,
}

internal data class TouchPoint(val x: Float, val y: Float)

internal data class TouchSample(
    val action: TouchAction,
    val points: List<TouchPoint>,
    val eventTimeMillis: Long,
    val width: Int,
    val height: Int,
)

internal class RemoteGestureInterpreter(
    private val emit: (InputCommand) -> Unit,
) {
    var mode: RemoteInputMode = RemoteInputMode.DirectTouch
    var touchpadSensitivity: Float = 1f

    private var downPoint = TouchPoint(0f, 0f)
    private var lastCentroid = TouchPoint(0f, 0f)
    private var downTimeMillis = 0L
    private var lastTapUpMillis = Long.MIN_VALUE
    private var maximumPointerCount = 0
    private var twoFingerMoved = false
    private var dragging = false

    fun onTouch(sample: TouchSample) {
        if (sample.width <= 0 || sample.height <= 0) return
        when (sample.action) {
            TouchAction.Down -> beginGesture(sample)
            TouchAction.PointerDown -> addPointer(sample)
            TouchAction.Move -> moveGesture(sample)
            TouchAction.PointerUp -> lastCentroid = centroid(sample.points)
            TouchAction.Up -> finishGesture(sample)
            TouchAction.Cancel -> cancelGesture()
        }
    }

    fun cancelGesture() {
        if (dragging) emit(InputCommand.MouseButton(RemoteMouseButton.Left, false))
        dragging = false
        maximumPointerCount = 0
        twoFingerMoved = false
    }

    private fun beginGesture(sample: TouchSample) {
        val point = sample.points.firstOrNull() ?: return
        downPoint = point
        lastCentroid = point
        downTimeMillis = sample.eventTimeMillis
        maximumPointerCount = 1
        twoFingerMoved = false
        dragging = mode == RemoteInputMode.Touchpad && lastTapUpMillis != Long.MIN_VALUE &&
            sample.eventTimeMillis - lastTapUpMillis <= DOUBLE_TAP_MILLIS
        if (dragging) emit(InputCommand.MouseButton(RemoteMouseButton.Left, true))
    }

    private fun addPointer(sample: TouchSample) {
        maximumPointerCount = maxOf(maximumPointerCount, sample.points.size)
        lastCentroid = centroid(sample.points)
        if (dragging) {
            emit(InputCommand.MouseButton(RemoteMouseButton.Left, false))
            dragging = false
        }
    }

    private fun moveGesture(sample: TouchSample) {
        if (sample.points.isEmpty()) return
        maximumPointerCount = maxOf(maximumPointerCount, sample.points.size)
        val current = centroid(sample.points)
        if (sample.points.size >= 2 || maximumPointerCount >= 2) {
            val deltaXRatio = (current.x - lastCentroid.x) / sample.width
            val deltaYRatio = (current.y - lastCentroid.y) / sample.height
            if (abs(deltaXRatio) >= WHEEL_THRESHOLD || abs(deltaYRatio) >= WHEEL_THRESHOLD) {
                val wheelX = (-deltaXRatio / WHEEL_THRESHOLD).roundToInt() * WHEEL_DELTA
                val wheelY = (-deltaYRatio / WHEEL_THRESHOLD).roundToInt() * WHEEL_DELTA
                if (wheelX != 0 || wheelY != 0) emit(InputCommand.Wheel(wheelX, wheelY))
                twoFingerMoved = true
                lastCentroid = current
            }
            return
        }

        if (mode == RemoteInputMode.Touchpad) {
            val deltaX = ((current.x - lastCentroid.x) / sample.width) * touchpadSensitivity
            val deltaY = ((current.y - lastCentroid.y) / sample.height) * touchpadSensitivity
            if (deltaX != 0f || deltaY != 0f) emit(InputCommand.MoveRelative(deltaX, deltaY))
            lastCentroid = current
            return
        }

        if (!dragging && sample.eventTimeMillis - downTimeMillis >= LONG_PRESS_MILLIS) {
            emit(buttonAt(RemoteMouseButton.Left, true, downPoint, sample))
            dragging = true
        }
        if (dragging) emit(absoluteMove(current, sample))
    }

    private fun finishGesture(sample: TouchSample) {
        val point = sample.points.firstOrNull() ?: lastCentroid
        when {
            dragging -> emit(
                if (mode == RemoteInputMode.DirectTouch) buttonAt(RemoteMouseButton.Left, false, point, sample)
                else InputCommand.MouseButton(RemoteMouseButton.Left, false),
            )
            maximumPointerCount >= 2 && !twoFingerMoved -> emitClick(
                RemoteMouseButton.Right,
                if (mode == RemoteInputMode.DirectTouch) point else null,
                sample,
            )
            maximumPointerCount == 1 -> emitClick(
                RemoteMouseButton.Left,
                if (mode == RemoteInputMode.DirectTouch) point else null,
                sample,
            )
        }
        if (maximumPointerCount == 1) lastTapUpMillis = sample.eventTimeMillis
        dragging = false
        maximumPointerCount = 0
        twoFingerMoved = false
    }

    private fun emitClick(button: RemoteMouseButton, point: TouchPoint?, sample: TouchSample) {
        if (point == null) {
            emit(InputCommand.MouseButton(button, true))
            emit(InputCommand.MouseButton(button, false))
        } else {
            emit(buttonAt(button, true, point, sample))
            emit(buttonAt(button, false, point, sample))
        }
    }

    private fun absoluteMove(point: TouchPoint, sample: TouchSample) = InputCommand.MoveAbsolute(
        (point.x / sample.width).coerceIn(0f, 1f),
        (point.y / sample.height).coerceIn(0f, 1f),
    )

    private fun buttonAt(button: RemoteMouseButton, down: Boolean, point: TouchPoint, sample: TouchSample) = InputCommand.MouseButton(
        button,
        down,
        (point.x / sample.width).coerceIn(0f, 1f),
        (point.y / sample.height).coerceIn(0f, 1f),
    )

    private fun centroid(points: List<TouchPoint>): TouchPoint {
        if (points.isEmpty()) return lastCentroid
        return TouchPoint(points.sumOf { it.x.toDouble() }.toFloat() / points.size, points.sumOf { it.y.toDouble() }.toFloat() / points.size)
    }

    private companion object {
        const val DOUBLE_TAP_MILLIS = 300L
        const val LONG_PRESS_MILLIS = 450L
        const val WHEEL_THRESHOLD = 0.015f
        const val WHEEL_DELTA = 120
    }
}
