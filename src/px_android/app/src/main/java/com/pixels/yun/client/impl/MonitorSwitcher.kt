package com.pixels.yun.client.impl

import android.app.Activity
import android.util.Log
import android.view.View
import android.widget.LinearLayout
import android.widget.RelativeLayout
import android.widget.TextView
import androidx.appcompat.widget.AppCompatImageButton
import com.pixels.yun.client.AppContext
import com.pixels.yun.client.R
import com.pixels.yun.client.impl.ThunderCallbacks.MonitorInfo
import com.pixels.yun.client.impl.ThunderCallbacks.ServerConfiguration

class MonitorSwitcher {

    companion object {
        val TAG = "Monitor"
    }

    class MonitorSwitcherItem {
        var itemView: View? = null
        var itemText: TextView? = null
        var itemIndicator: View? = null
        var itemMonitorBtn: AppCompatImageButton? = null
    }

    private var switchers: MutableList<MonitorSwitcherItem> = mutableListOf()
    private var monitorsInfo: MutableList<MonitorInfo> = mutableListOf()

    constructor(activity: Activity, appContext: AppContext, thunderApp: ThunderApp) {
        val switchParent = activity.findViewById<LinearLayout>(R.id.id_monitor_switch_parent)
        for (i in 0 until 8) {
            val view = View.inflate(activity, R.layout.item_monitor_switch, null)
            switchParent.addView(view)

            val item = MonitorSwitcherItem()
            item.itemView = view
            item.itemText = view.findViewById(R.id.id_monitor_index)
            item.itemIndicator = view.findViewById(R.id.id_monitor_indicator)
            item.itemMonitorBtn = view.findViewById(R.id.id_monitor_btn)
            switchers.add(item)

            item.itemText!!.text = (i+1).toString()

            // click callback
            item.itemMonitorBtn!!.setOnClickListener {
                if (monitorsInfo.size > i) {
                    thunderApp.changeMonitor(i, monitorsInfo[i].name)
                }
                else {
                    Log.e(TAG, "don't have target index: $i")
                }
            }
        }
        hideAllButtons()
    }

    fun updateMonitorInfo(config: ServerConfiguration) {
        this.monitorsInfo.clear()
        hideAllButtons()

        var index = 0;
        config.monitors.forEach {
            this.monitorsInfo.add(it)
            switchers[index].itemView!!.visibility = View.VISIBLE
            Log.i(TAG, "show item index: $index, name : ${it.name}")
            if (config.capturingMonitorName == it.name) {
                switchers[index].itemIndicator!!.visibility = View.VISIBLE
            }
            else {
                switchers[index].itemIndicator!!.visibility = View.GONE
            }

            index++
        }
    }

    private fun hideAllButtons() {
        switchers.forEach {
            it.itemView!!.visibility = View.GONE
        }
    }

}