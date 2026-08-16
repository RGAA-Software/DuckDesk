package com.pixels.yun.client

import com.pixels.yun.client.db.DBServer

class ScanInfo {

    class IpInfo {
        var ip: String = ""
        var type: String = ""

        override fun toString(): String {
            return "IpInfo(ip='$ip', type='$type')"
        }
    }

    var deviceId: String = ""

    var deviceRandomPwd: String = "";

    var iconIndex: Int = 0

    var panelServerPort: Int = 0

    var streamWssPort: Int = 0

    var deviceIpInfo: MutableList<IpInfo> = mutableListOf<IpInfo>()

    var workingIp: String = ""
    var workingIpType: String = ""

    fun valid(): Boolean {
        return deviceId.isNotEmpty() && panelServerPort > 0
    }

    fun canConnect(): Boolean {
        return workingIp.isNotEmpty();
    }

    fun asDBServer(): DBServer {
        val s = DBServer();
        s.serverId = this.deviceId
        s.iconIndex = this.iconIndex
        s.serverName = ""
        s.serverIp = this.workingIp
        s.serverVersion = ""
        s.httpServerPort = this.panelServerPort
        s.streamWsPort = this.streamWssPort
        s.coverUrl = ""
        return s;
    }

    fun hasTargetIp(ip: String): Boolean {
        deviceIpInfo.forEach {
            if (it.ip == ip) {
                return true
            }
        }
        return false
    }

    override fun toString(): String {
        return "ScanInfo(deviceId='$deviceId', deviceRandomPwd='$deviceRandomPwd', iconIndex=$iconIndex, panelServerPort=$panelServerPort, streamWssPort=$streamWssPort, deviceIpInfo=$deviceIpInfo, workingIp='$workingIp', workingIpType='$workingIpType')"
    }


}