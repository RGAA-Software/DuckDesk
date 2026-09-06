package yun.pixels.client.core.nativebridge

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONObject
import yun.pixels.client.core.domain.device.DeviceEndpoint
import java.net.HttpURLConnection
import java.net.URI
import java.nio.charset.StandardCharsets
import java.util.UUID

internal data class DirectSessionAuthorization(
    val streamId: String,
    val clientNonce: String,
)

internal sealed interface DirectSessionAuthorizationResult {
    data class Authorized(val value: DirectSessionAuthorization) : DirectSessionAuthorizationResult
    data object Rejected : DirectSessionAuthorizationResult
    data object Unavailable : DirectSessionAuthorizationResult
}

internal fun interface DirectSessionAuthorizer {
    suspend fun authorize(endpoint: DeviceEndpoint, credential: String): DirectSessionAuthorizationResult
}

internal class HttpDirectSessionAuthorizer(
    private val now: () -> Long = System::currentTimeMillis,
) : DirectSessionAuthorizer {
    override suspend fun authorize(endpoint: DeviceEndpoint, credential: String): DirectSessionAuthorizationResult = withContext(Dispatchers.IO) {
        if (!endpoint.host.isPrivateOrCarrierGradeAddress() || endpoint.renderPort !in 1..65535) {
            return@withContext DirectSessionAuthorizationResult.Rejected
        }
        val clientNonce = UUID.randomUUID().toString()
        val query = "safety_pwd_md5=$credential&client_nonce=$clientNonce"
        val connection = runCatching {
            URI("http", null, endpoint.host, endpoint.renderPort, VERIFY_PATH, query, null).toURL().openConnection() as HttpURLConnection
        }.getOrNull() ?: return@withContext DirectSessionAuthorizationResult.Unavailable

        try {
            connection.requestMethod = "GET"
            connection.connectTimeout = CONNECT_TIMEOUT_MILLIS
            connection.readTimeout = READ_TIMEOUT_MILLIS
            connection.instanceFollowRedirects = false
            connection.useCaches = false
            connection.setRequestProperty("Accept", "application/json")
            val status = connection.responseCode
            if (status == HttpURLConnection.HTTP_FORBIDDEN || status == HttpURLConnection.HTTP_UNAUTHORIZED) {
                return@withContext DirectSessionAuthorizationResult.Rejected
            }
            if (status !in 200..299) return@withContext DirectSessionAuthorizationResult.Unavailable
            val response = connection.inputStream.bufferedReader(StandardCharsets.UTF_8).use { it.readText() }
            parseDirectSessionAuthorization(response, clientNonce, now())
        } catch (_: Exception) {
            DirectSessionAuthorizationResult.Unavailable
        } finally {
            connection.disconnect()
        }
    }

    private companion object {
        const val VERIFY_PATH = "/verify/security/password"
        const val CONNECT_TIMEOUT_MILLIS = 3_500
        const val READ_TIMEOUT_MILLIS = 3_500
    }
}

internal fun parseDirectSessionAuthorization(
    response: String,
    clientNonce: String,
    nowEpochMillis: Long,
): DirectSessionAuthorizationResult = runCatching {
    val envelope = JSONObject(response)
    if (envelope.optInt("code", -1) != 200) return DirectSessionAuthorizationResult.Rejected
    val data = envelope.optJSONObject("data") ?: return DirectSessionAuthorizationResult.Rejected
    val streamId = data.optString("stream_id").trim()
    val expiresAt = data.optLong("expires_at_ms", 0L)
    if (!streamId.startsWith("ip-direct:") || clientNonce.isBlank() || expiresAt <= nowEpochMillis) {
        return DirectSessionAuthorizationResult.Rejected
    }
    DirectSessionAuthorizationResult.Authorized(DirectSessionAuthorization(streamId, clientNonce))
}.getOrDefault(DirectSessionAuthorizationResult.Rejected)
