package com.pixels.yun.client.effects

import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.LinearGradient
import android.graphics.Paint
import android.graphics.Shader
import android.util.AttributeSet
import android.view.View
import com.pixels.yun.client.impl.ThunderApp

/**
 * Native (Canvas) replacement for the old libgdx BarLine spectrum effect.
 *
 * Draws a row of vertical bars that react to the audio spectrum, with the same
 * smoothing behavior the libgdx EffectView used to apply.
 */
class SpectrumView @JvmOverloads constructor(
    context: Context,
    attrs: AttributeSet? = null,
    defStyleAttr: Int = 0
) : View(context, attrs, defStyleAttr) {

    private var thunderApp: ThunderApp? = null

    private val leftSpectrum = mutableListOf<Double>()

    private val barPaint = Paint(Paint.ANTI_ALIAS_FLAG)

    // Same gradient colors the original BarLine used.
    private val fromColor = Color.rgb(0x72, 0xED, 0xF2)
    private val toColor = Color.rgb(0x51, 0x51, 0xE5)

    fun bind(app: ThunderApp) {
        thunderApp = app
    }

    /** Pull the latest spectrum, smooth it, and request a redraw. */
    fun refresh() {
        val app = thunderApp ?: return
        val latest = app.leftSpectrum ?: return
        synchronized(latest) {
            if (latest.isEmpty()) {
                return
            }
            if (leftSpectrum.size != latest.size) {
                leftSpectrum.clear()
                leftSpectrum.addAll(latest)
            } else {
                latest.forEachIndexed { index, newValue ->
                    val oldValue = leftSpectrum[index]
                    val diff = newValue - oldValue
                    var targetValue = oldValue + diff / 3.0
                    if (targetValue < 0.0) {
                        targetValue = 0.0
                    }
                    leftSpectrum[index] = targetValue
                }
            }
        }
        invalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        if (leftSpectrum.isEmpty() || width <= 0 || height <= 0) {
            return
        }

        val w = width.toFloat()
        val h = height.toFloat()
        val n = leftSpectrum.size

        // Match the original BarLine layout: itemWidth is 1.2x an even split.
        val itemWidth = w / n * 1.2f
        val xStep = itemWidth

        val gradient = LinearGradient(0f, 0f, 0f, h, fromColor, toColor, Shader.TileMode.CLAMP)
        barPaint.shader = gradient

        leftSpectrum.forEachIndexed { index, value ->
            val xLeft = index * xStep
            val barHeight = (value.toFloat() * 3.6f).coerceIn(0f, h)
            // Draw the bar from the bottom edge upward.
            canvas.drawRect(xLeft, h - barHeight, xLeft + xStep, h, barPaint)
        }
    }
}
