package yun.pixels.client.core.data

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.DeviceResolution
import yun.pixels.client.core.domain.device.DeviceResolutionFailure
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.domain.device.RemoteDevice
import yun.pixels.client.core.domain.device.ResolvedDevice
import java.net.HttpURLConnection
import java.net.Inet4Address
import java.net.InetAddress
import java.net.URI
import java.nio.charset.StandardCharsets
import java.util.Base64

class PanelDeviceResolver(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    private val now: () -> Long = System::currentTimeMillis,
    private val connectTimeoutMillis: Int = DEFAULT_CONNECT_TIMEOUT_MILLIS,
    private val readTimeoutMillis: Int = DEFAULT_READ_TIMEOUT_MILLIS,
) : DeviceResolver {
    override suspend fun resolve(connectionInput: String): DeviceResolution = withContext(ioDispatcher) {
        val target = ConnectionInputParser.parse(connectionInput)
            ?: return@withContext DeviceResolution.Failure(DeviceResolutionFailure.InvalidInput)

        val endpointsByScope = target.endpoints.map { endpoint -> endpoint to endpoint.host.networkScope() }
        val localEndpoints = endpointsByScope.filter { (_, scope) -> scope == NetworkScope.Local }.map { (endpoint) -> endpoint }
        if (localEndpoints.isEmpty()) {
            val failure = if (endpointsByScope.any { (_, scope) -> scope == NetworkScope.Public }) {
                DeviceResolutionFailure.PublicNetworkAddress
            } else {
                DeviceResolutionFailure.Unreachable
            }
            return@withContext DeviceResolution.Failure(failure)
        }

        var sawInvalidResponse = false
        localEndpoints.forEach { endpoint ->
            when (val result = fetchDevice(endpoint)) {
                is FetchResult.Success -> {
                    if (target.expectedDeviceId != null && target.expectedDeviceId != result.resolved.device.id.value) {
                        sawInvalidResponse = true
                    } else {
                        val password = result.resolved.oneTimePassword ?: target.oneTimePassword
                        return@withContext DeviceResolution.Success(result.resolved.copy(oneTimePassword = password))
                    }
                }
                FetchResult.InvalidResponse -> sawInvalidResponse = true
                FetchResult.Unreachable -> Unit
            }
        }

        DeviceResolution.Failure(
            if (sawInvalidResponse) DeviceResolutionFailure.InvalidResponse else DeviceResolutionFailure.Unreachable,
        )
    }

    private fun fetchDevice(endpoint: DeviceEndpoint): FetchResult {
        val connection = runCatching {
            URI("http", null, endpoint.host, endpoint.panelPort, SIMPLE_INFO_PATH, null, null)
                .toURL()
                .openConnection() as HttpURLConnection
        }.getOrNull() ?: return FetchResult.Unreachable

        return try {
            connection.requestMethod = "GET"
            connection.connectTimeout = connectTimeoutMillis
            connection.readTimeout = readTimeoutMillis
            connection.instanceFollowRedirects = false
            connection.useCaches = false
            connection.setRequestProperty("Accept", "application/json")

            if (connection.responseCode !in 200..299) return FetchResult.InvalidResponse
            val response = connection.inputStream.bufferedReader(StandardCharsets.UTF_8).use { it.readText() }
            parseResponse(response, endpoint)
        } catch (_: Exception) {
            FetchResult.Unreachable
        } finally {
            connection.disconnect()
        }
    }

    private fun parseResponse(response: String, sourceEndpoint: DeviceEndpoint): FetchResult = runCatching {
        val envelope = JSONObject(response)
        if (envelope.getInt("code") != 200) return FetchResult.InvalidResponse
        val payload = envelope.getJSONObject("data")
        val deviceId = payload.getString("did").trim()
        if (deviceId.isEmpty()) return FetchResult.InvalidResponse

        val panelPort = payload.getInt("ppt")
        val renderPort = payload.getInt("rdpt")
        val endpoint = DeviceEndpoint(sourceEndpoint.host, panelPort, renderPort)
        val displayName = payload.optString("dn").trim().ifEmpty { sourceEndpoint.host }
        val password = payload.optString("rpwd").takeIf(String::isNotBlank)
        FetchResult.Success(
            ResolvedDevice(
                device = RemoteDevice(
                    id = DeviceId(deviceId),
                    displayName = displayName,
                    platformName = "Windows",
                    availability = DeviceAvailability.Online,
                    endpoint = endpoint,
                    lastSeenEpochMillis = now(),
                ),
                oneTimePassword = password,
            ),
        )
    }.getOrElse { FetchResult.InvalidResponse }

    private sealed interface FetchResult {
        data class Success(val resolved: ResolvedDevice) : FetchResult

        object Unreachable : FetchResult
        object InvalidResponse : FetchResult
    }

    companion object {
        private const val SIMPLE_INFO_PATH = "/v1/simple/info"
        private const val DEFAULT_CONNECT_TIMEOUT_MILLIS = 3_500
        private const val DEFAULT_READ_TIMEOUT_MILLIS = 3_500
    }
}

internal data class ConnectionTarget(
    val endpoints: List<DeviceEndpoint>,
    val expectedDeviceId: String? = null,
    val oneTimePassword: String? = null,
)

internal object ConnectionInputParser {
    fun parse(input: String): ConnectionTarget? {
        val normalized = input.trim()
        if (normalized.isEmpty()) return null
        return if (normalized.startsWith(LINK_PREFIX, ignoreCase = true)) {
            parseLink(normalized.substring(LINK_PREFIX.length))
        } else {
            parseHost(normalized)
        }
    }

    private fun parseHost(input: String): ConnectionTarget? = runCatching {
        val uri = URI(if ("://" in input) input else "http://$input")
        if (!uri.scheme.equals("http", ignoreCase = true)) return null
        if (uri.userInfo != null || uri.query != null || uri.fragment != null) return null
        if (uri.path?.let { it.isNotEmpty() && it != "/" } == true) return null
        val host = uri.host?.trim()?.takeIf(String::isNotEmpty) ?: return null
        val panelPort = if (uri.port == -1) DeviceEndpoint.DEFAULT_PANEL_PORT else uri.port
        ConnectionTarget(listOf(DeviceEndpoint(host, panelPort, DeviceEndpoint.DEFAULT_RENDER_PORT)))
    }.getOrNull()

    private fun parseLink(encodedPayload: String): ConnectionTarget? = runCatching {
        val decoded = decodeBase64(encodedPayload)
        val payload = JSONObject(decoded.toString(StandardCharsets.UTF_8))
        val deviceId = payload.getString("did").trim().takeIf(String::isNotEmpty) ?: return null
        val panelPort = payload.getInt("ppt")
        val renderPort = payload.getInt("rdpt")
        val hosts = payload.getJSONArray("ips")
        val endpoints = buildList {
            repeat(hosts.length()) { index ->
                val host = hosts.getJSONObject(index).getString("ip").trim()
                if (host.isNotEmpty()) add(DeviceEndpoint(host, panelPort, renderPort))
            }
        }.distinct()
        if (endpoints.isEmpty()) return null
        ConnectionTarget(
            endpoints = endpoints,
            expectedDeviceId = deviceId,
            oneTimePassword = payload.optString("rpwd").takeIf(String::isNotBlank),
        )
    }.getOrNull()

    private fun decodeBase64(payload: String): ByteArray {
        val compact = payload.filterNot(Char::isWhitespace)
        return runCatching { Base64.getDecoder().decode(compact) }
            .getOrElse { Base64.getUrlDecoder().decode(compact) }
    }

    private const val LINK_PREFIX = "link://"
}

private enum class NetworkScope {
    Local,
    Public,
    Unresolved,
}

private fun String.networkScope(): NetworkScope = runCatching {
    val addresses = InetAddress.getAllByName(this)
    if (addresses.all { address ->
        address.isAnyLocalAddress || address.isLoopbackAddress || address.isLinkLocalAddress || address.isSiteLocalAddress || address.isCarrierGradeNat()
        }) {
        NetworkScope.Local
    } else {
        NetworkScope.Public
    }
}.getOrDefault(NetworkScope.Unresolved)

private fun InetAddress.isCarrierGradeNat(): Boolean {
    if (this !is Inet4Address) return false
    val octets = address.map(Byte::toInt).map { it and 0xff }
    return octets[0] == 100 && octets[1] in 64..127
}
