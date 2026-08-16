package com.pixels.yun.client

import android.content.Context
import android.text.TextUtils
import android.util.Base64
import android.util.Log
import com.pixels.yun.client.db.DBServer
import com.pixels.yun.client.events.OnServerAvailable
import com.pixels.yun.client.events.OnServerDeleted
import com.pixels.yun.client.events.OnServerEmpty
import com.pixels.yun.client.events.OnServerOffline
import com.pixels.yun.client.events.OnServerScanned
import com.pixels.yun.client.ui.steam.SteamAppFragment
import com.pixels.yun.client.util.SpUtils
import org.greenrobot.eventbus.EventBus
import org.greenrobot.eventbus.Subscribe
import org.greenrobot.eventbus.ThreadMode
import org.json.JSONObject

class Settings {

    var deviceId: String = ""

    companion object {
        const val TAG = "Main"
        const val KEY_SHOW_VIRTUAL_GAMEPAD = "show_virtual_gamepad"
        const val KEY_INVERT_JOYSTICK_Y_AXIS = "invert_joystick_y_axis"
        const val KEY_SHOW_CURSOR = "show_cursor"
        const val KEY_FULLSCREEN = "fullscreen"
        const val KEY_DEVICE_ID = "device_id"
        const val KEY_MONITOR_INDICATOR = "monitor_indicator"
        const val KEY_SHOW_LOGO = "monitor_show_logo"
        const val KEY_STREAM_ID = "stream_id"

        private val settings = Settings()
        fun getInstance(): Settings {
            return settings
        }
    }

    fun loadConfig(ctx: Context) {
        deviceId = SpUtils.getInstance(ctx).getString(KEY_DEVICE_ID)
        if (TextUtils.isEmpty(deviceId)) {
            val androidId: String =
                android.provider.Settings.System.getString(ctx.contentResolver, android.provider.Settings.Secure.ANDROID_ID)
            deviceId = androidId
            SpUtils.getInstance(ctx).put(KEY_DEVICE_ID, deviceId)
        }
        EventBus.getDefault().register(this);
    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerAvailableEvent(event: OnServerAvailable) {

    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerOfflineEvent(event: OnServerOffline) {

    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerDeletedEvent(event: OnServerDeleted) {

    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onServerEmptyEvent(event: OnServerEmpty) {
    }

    @Subscribe(threadMode = ThreadMode.POSTING)
    fun onMessageEvent(event: OnServerScanned) {

    }

    fun parseScanInfo(info: String): ScanInfo {
        Log.i(TAG, "scan info: $info")
        val scanInfo = ScanInfo();
        try {
            val obj = JSONObject(info);
            scanInfo.deviceId = obj.getString("did");
            scanInfo.iconIndex = obj.getInt("iidx");
            scanInfo.deviceRandomPwd = obj.getString("rpwd");
            scanInfo.panelServerPort = obj.getInt("ppt");
            scanInfo.streamWssPort = obj.getInt("rdpt");
            val ips = obj.getJSONArray("ips");
            for (i in 0 until ips.length()) {
                val ipInfo = ScanInfo.IpInfo();
                val ipInfoObj = ips.getJSONObject(i);
                ipInfo.ip = ipInfoObj.getString("ip");
                scanInfo.deviceIpInfo.add(ipInfo);
            }
            if (scanInfo.deviceId.isEmpty() && scanInfo.deviceIpInfo.isNotEmpty()) {
                scanInfo.deviceId = scanInfo.deviceIpInfo[0].ip
            }
            Log.i(TAG, "scanInfo: $scanInfo")
        } catch (e: Exception) {
            e.printStackTrace()
            Log.e(TAG, "parse scan info failed: " + e.message)
        }
        return scanInfo;
    }

    fun setShowVirtualGamepad(ctx: Context, state: Boolean) {
        SpUtils.getInstance(ctx).put(KEY_SHOW_VIRTUAL_GAMEPAD, state)
    }

    fun isShowVirtualGamepad(ctx: Context): Boolean {
        return SpUtils.getInstance(ctx).getBoolean(KEY_SHOW_VIRTUAL_GAMEPAD)
    }

    fun setInvertJoystickYAxis(ctx: Context, state: Boolean) {
        SpUtils.getInstance(ctx).put(KEY_INVERT_JOYSTICK_Y_AXIS, state)
    }

    fun isInvertJoystickYAxis(ctx: Context): Boolean {
        return SpUtils.getInstance(ctx).getBoolean(KEY_INVERT_JOYSTICK_Y_AXIS)
    }

    fun setShowCursor(ctx: Context, state: Boolean) {
        SpUtils.getInstance(ctx).put(KEY_SHOW_CURSOR, state)
    }

    fun isShowCursor(ctx: Context): Boolean {
        return SpUtils.getInstance(ctx).getBoolean(KEY_SHOW_CURSOR)
    }

    fun setFullscreen(ctx: Context, state: Boolean) {
        SpUtils.getInstance(ctx).put(KEY_FULLSCREEN, state)
    }

    fun isFullscreen(ctx: Context) : Boolean{
        return SpUtils.getInstance(ctx).getBoolean(KEY_FULLSCREEN, true)
    }

    fun isShowMonitorIndicator(ctx: Context): Boolean {
        return SpUtils.getInstance(ctx).getBoolean(KEY_MONITOR_INDICATOR, true)
    }

    fun setShowMonitorIndicator(ctx: Context, state: Boolean) {
        SpUtils.getInstance(ctx).put(KEY_MONITOR_INDICATOR, state)
    }

    fun isShowLogo(ctx: Context): Boolean {
        return SpUtils.getInstance(ctx).getBoolean(KEY_SHOW_LOGO, true)
    }

    fun setShowLogo(ctx: Context, state: Boolean) {
        SpUtils.getInstance(ctx).put(KEY_SHOW_LOGO, state)
    }

    fun dump() {

    }
}