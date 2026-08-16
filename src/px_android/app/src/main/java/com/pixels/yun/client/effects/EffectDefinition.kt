package com.pixels.yun.client.effects

import com.pixels.yun.client.R

class EffectDefinition {

    companion object {
        const val EFFECT_SPECTRUM = 1
    }

    class EffectInfo(var idx: Int, var name: String, var iconResId: Int)

    val effects: MutableList<EffectInfo> = mutableListOf()

    fun init() {
        effects.add(EffectInfo(EFFECT_SPECTRUM, "Spectrum", R.drawable.effect_bar_line))
    }
}
