package com.pixels.yun.client.ui

import android.app.Activity
import android.content.Context
import android.os.Bundle
import androidx.fragment.app.Fragment
import com.pixels.yun.client.App
import com.pixels.yun.client.AppContext

open class BaseFragment(): Fragment() {

    lateinit var appContext: AppContext

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        appContext = (requireActivity().application as App).appContext
    }

    open fun onRefresh() {

    }

}