package yun.pixels.client.core.nativebridge

import org.json.JSONObject
import org.webrtc.PeerConnection

internal data class RtcIceConfiguration(
    val revision: Long,
    val directProbeEnabled: Boolean,
    val expiresAtEpochSeconds: Long,
    val servers: List<RtcIceServer>,
) {
    fun toWebRtcServers(): List<PeerConnection.IceServer> = servers.map { server ->
        PeerConnection.IceServer.builder(server.urls)
            .setUsername(server.username)
            .setPassword(server.credential)
            .createIceServer()
    }
}

internal data class RtcIceServer(
    val id: String,
    val urls: List<String>,
    val username: String,
    val credential: String,
)

internal fun parseRtcIceConfiguration(json: String): RtcIceConfiguration? {
    if (json.isBlank() || json.length > MAX_RTC_ICE_CONFIG_CHARS) return null
    return runCatching {
        val root = JSONObject(json)
        val revision = root.optLong("revision", -1L).takeIf { it >= 0L } ?: return null
        val expiresAt = root.optLong("expires_at", -1L).takeIf { it > 0L } ?: return null
        val array = root.optJSONArray("ice_servers") ?: return null
        if (array.length() !in 1..MAX_RTC_ICE_SERVERS) return null
        val servers = buildList(array.length()) {
            repeat(array.length()) { index ->
                val value = array.optJSONObject(index) ?: return null
                val id = value.optString("id").trim().takeIf { it.isNotEmpty() } ?: return null
                val urlArray = value.optJSONArray("urls") ?: return null
                if (urlArray.length() !in 1..MAX_RTC_ICE_URLS_PER_SERVER) return null
                val urls = buildList(urlArray.length()) {
                    repeat(urlArray.length()) { urlIndex ->
                        val url = urlArray.optString(urlIndex).trim()
                        if (!url.isSupportedIceUrl()) return null
                        add(url)
                    }
                }.distinct()
                add(
                    RtcIceServer(
                        id = id,
                        urls = urls,
                        username = value.optString("username"),
                        credential = value.optString("credential"),
                    ),
                )
            }
        }
        RtcIceConfiguration(
            revision = revision,
            directProbeEnabled = root.optBoolean("direct_probe_enabled", false),
            expiresAtEpochSeconds = expiresAt,
            servers = servers,
        )
    }.getOrNull()
}

private fun String.isSupportedIceUrl(): Boolean {
    if (length > MAX_RTC_ICE_URL_CHARS || any(Char::isWhitespace)) return false
    return startsWith("stun:", ignoreCase = true) ||
        startsWith("turn:", ignoreCase = true) ||
        startsWith("turns:", ignoreCase = true)
}

private const val MAX_RTC_ICE_CONFIG_CHARS = 64 * 1024
private const val MAX_RTC_ICE_SERVERS = 16
private const val MAX_RTC_ICE_URLS_PER_SERVER = 8
private const val MAX_RTC_ICE_URL_CHARS = 2048
