package yun.pixels.client.core.network

import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.JoinMode
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import javax.net.ssl.HttpsURLConnection

interface ConsoleAccountApi {
    suspend fun login(endpointInput: String, username: String, password: String): AccountResult<AccountSession>

    suspend fun logout(session: AccountSession): AccountResult<Unit>

    suspend fun devices(session: AccountSession): AccountResult<List<AccountDevice>>

    suspend fun issueTicket(
        session: AccountSession,
        deviceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket>

    suspend fun renewTicket(
        session: AccountSession,
        ticket: ConnectionTicket,
        clientNonce: String,
    ): AccountResult<ConnectionTicket>
}

class ConsoleApiClient(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ConsoleAccountApi, ConsoleApplicationApi {
    override suspend fun login(endpointInput: String, username: String, password: String): AccountResult<AccountSession> = withContext(ioDispatcher) {
        val endpoint = normalizeEndpoint(endpointInput)
            ?: return@withContext AccountResult.Failure(AccountFailure.InvalidEndpoint)
        val body = JSONObject()
            .put("username", username.trim())
            .put("password", password)
            .put("client_type", "panel")
        request(endpoint, "/api/v1/session/user/login", "POST", body = body)?.toAccountResult { response ->
            parseLogin(endpoint, response as JSONObject)
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun logout(session: AccountSession): AccountResult<Unit> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/v1/session/user/logout", "POST", session.accessToken, JSONObject())
            ?.toAccountResult { AccountResult.Success(Unit) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun devices(session: AccountSession): AccountResult<List<AccountDevice>> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/v1/user/devices", "GET", session.accessToken)?.toAccountResult { data ->
            parseDevices(data as JSONArray)
        }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun issueTicket(
        session: AccountSession,
        deviceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = withContext(ioDispatcher) {
        val encodedDeviceId = URLEncoder.encode(deviceId, StandardCharsets.UTF_8.name()).replace("+", "%20")
        val body = JSONObject()
            .put("client_nonce", clientNonce)
            .put("join_mode", if (joinMode == JoinMode.Control) "control" else "observe")
        request(
            session.endpoint,
            "/api/v1/user/devices/$encodedDeviceId/ticket",
            "POST",
            session.accessToken,
            body,
        )?.toAccountResult { data -> parseTicket(data as JSONObject) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/v1/user/apps", "GET", session.accessToken)?.toAccountResult { data ->
            AccountResult.Success(parseApplications(data as JSONArray))
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun startApplication(
        session: AccountSession,
        appId: String,
        clientNonce: String,
    ): AccountResult<RemoteApplicationInstance> = withContext(ioDispatcher) {
        val encodedAppId = encodePathSegment(appId)
        val body = JSONObject().put("client_nonce", clientNonce)
        request(session.endpoint, "/api/v1/user/apps/$encodedAppId/start", "POST", session.accessToken, body)?.toAccountResult { data ->
            AccountResult.Success(parseApplicationInstance(data as JSONObject))
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun stopApplication(session: AccountSession, instanceId: String): AccountResult<Unit> = withContext(ioDispatcher) {
        val encodedInstanceId = encodePathSegment(instanceId)
        val body = JSONObject().put("reason", "stopped from Pixels Android")
        request(session.endpoint, "/api/v1/user/instances/$encodedInstanceId/stop", "POST", session.accessToken, body)?.toAccountResult {
            AccountResult.Success(Unit)
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun issueApplicationTicket(
        session: AccountSession,
        instanceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = withContext(ioDispatcher) {
        val encodedInstanceId = encodePathSegment(instanceId)
        val body = JSONObject()
            .put("client_nonce", clientNonce)
            .put("join_mode", if (joinMode == JoinMode.Control) "control" else "observe")
        request(
            session.endpoint,
            "/api/v1/user/instances/$encodedInstanceId/ticket",
            "POST",
            session.accessToken,
            body,
        )?.toAccountResult { data -> parseTicket(data as JSONObject) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun renewTicket(
        session: AccountSession,
        ticket: ConnectionTicket,
        clientNonce: String,
    ): AccountResult<ConnectionTicket> = withContext(ioDispatcher) {
        if (ticket.renewalToken.isBlank() || clientNonce.isBlank()) {
            return@withContext AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        val body = JSONObject()
            .put("renewal_token", ticket.renewalToken)
            .put("client_nonce", clientNonce)
        request(
            session.endpoint,
            "/api/v1/connection-tickets/renew",
            "POST",
            body = body,
        )?.toAccountResult { data -> parseTicket(data as JSONObject, ticket) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    private fun request(
        endpoint: ConsoleEndpoint,
        path: String,
        method: String,
        bearerToken: String? = null,
        body: JSONObject? = null,
    ): HttpResponse? {
        val connection = runCatching {
            URI(endpoint.baseUrl).resolve(path).toURL().openConnection() as HttpsURLConnection
        }.getOrNull() ?: return null
        return try {
            connection.requestMethod = method
            connection.connectTimeout = CONNECT_TIMEOUT_MILLIS
            connection.readTimeout = READ_TIMEOUT_MILLIS
            connection.instanceFollowRedirects = false
            connection.useCaches = false
            connection.setRequestProperty("Accept", "application/json")
            connection.setRequestProperty("User-Agent", "Pixels-Android/1")
            bearerToken?.let { connection.setRequestProperty("Authorization", "Bearer $it") }
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                connection.outputStream.use { output -> output.write(body.toString().toByteArray(StandardCharsets.UTF_8)) }
            }
            val status = connection.responseCode
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            val responseBody = stream?.bufferedReader(StandardCharsets.UTF_8)?.use { it.readText() }.orEmpty()
            HttpResponse(status, responseBody)
        } catch (_: Exception) {
            null
        } finally {
            connection.disconnect()
        }
    }

    companion object {
        private const val CONNECT_TIMEOUT_MILLIS = 5_000
        private const val READ_TIMEOUT_MILLIS = 8_000
    }
}

private fun encodePathSegment(value: String): String = URLEncoder.encode(value, StandardCharsets.UTF_8.name()).replace("+", "%20")

internal data class HttpResponse(val status: Int, val body: String)

internal fun normalizeEndpoint(input: String): ConsoleEndpoint? = runCatching {
    val normalized = input.trim().trimEnd('/')
    val uri = URI(normalized)
    if (!uri.scheme.equals("https", ignoreCase = true)) return null
    if (uri.host.isNullOrBlank() || uri.userInfo != null || uri.query != null || uri.fragment != null) return null
    if (!uri.path.isNullOrEmpty()) return null
    if (uri.port != -1 && uri.port !in 1..65535) return null
    ConsoleEndpoint(uri.toASCIIString())
}.getOrNull()

private inline fun <T> HttpResponse.toAccountResult(parse: (Any) -> AccountResult<T>): AccountResult<T> {
    if (status !in 200..299) return AccountResult.Failure(accountFailure(status, body))
    return runCatching {
        val envelope = JSONObject(body)
        if (envelope.optInt("code", 200) != 200) {
            AccountResult.Failure(AccountFailure.ServerError)
        } else {
            parse(envelope.get("data"))
        }
    }.getOrElse { AccountResult.Failure(AccountFailure.InvalidResponse) }
}

internal fun accountFailure(status: Int, body: String): AccountFailure {
    val error = runCatching { JSONObject(body).optString("error") }.getOrDefault("")
    return when (error) {
        "AUTH_INVALID_CREDENTIALS" -> AccountFailure.InvalidCredentials
        "AUTH_REQUIRED", "TICKET_EXPIRED_OR_USED" -> AccountFailure.AuthenticationRequired
        "SUBJECT_FORBIDDEN" -> AccountFailure.Forbidden
        "RATE_LIMITED", "QUOTA_EXCEEDED" -> AccountFailure.RateLimited
        "DEVICE_OFFLINE" -> AccountFailure.DeviceOffline
        "RESOURCE_NOT_FOUND" -> AccountFailure.NotFound
        else -> status.toAccountFailure()
    }
}

private fun Int.toAccountFailure(): AccountFailure = when (this) {
    401 -> AccountFailure.AuthenticationRequired
    403 -> AccountFailure.Forbidden
    404 -> AccountFailure.NotFound
    429 -> AccountFailure.RateLimited
    in 500..599 -> AccountFailure.ServerError
    else -> AccountFailure.InvalidCredentials
}

private fun parseLogin(endpoint: ConsoleEndpoint, data: JSONObject): AccountResult<AccountSession> {
    val profileJson = data.getJSONObject("profile")
    val accessToken = data.getString("access_token").takeIf(String::isNotBlank)
        ?: return AccountResult.Failure(AccountFailure.InvalidResponse)
    val profile = AccountProfile(
        userId = profileJson.getString("uid"),
        username = profileJson.getString("username"),
        avatarPath = profileJson.optString("avatar_path").takeIf(String::isNotBlank),
        mustChangePassword = profileJson.optBoolean("must_change_password", false),
    )
    return AccountResult.Success(
        AccountSession(
            endpoint = endpoint,
            profile = profile,
            accessToken = accessToken,
            expiresAtEpochMillis = data.getLong("expires_at"),
            absoluteExpiresAtEpochMillis = data.getLong("absolute_expires_at"),
        ),
    )
}

private fun parseDevices(data: JSONArray): AccountResult<List<AccountDevice>> = AccountResult.Success(
    buildList {
        repeat(data.length()) { index ->
            val item = data.getJSONObject(index)
            add(
                AccountDevice(
                    deviceId = item.getString("device_id"),
                    displayName = item.optString("name").ifBlank { item.getString("device_id") },
                    online = item.optBoolean("online", false),
                    lastSeenEpochMillis = item.optLong("last_seen_at").takeUnless { it == 0L },
                ),
            )
        }
    },
)

private fun parseApplications(data: JSONArray): List<RemoteApplication> = buildList {
    repeat(data.length()) { index ->
        val item = data.getJSONObject(index)
        val running = item.optJSONObject("running_instance")
        add(
            RemoteApplication(
                appId = item.getString("app_id"),
                name = item.optString("name").ifBlank { item.getString("app_id") },
                coverUrl = item.optString("cover_url"),
                runningInstance = running?.let(::parseApplicationInstance),
            ),
        )
    }
}

private fun parseApplicationInstance(data: JSONObject): RemoteApplicationInstance = RemoteApplicationInstance(
    instanceId = data.getString("instance_id"),
    state = when (data.optString("state").lowercase()) {
        "starting" -> RemoteApplicationInstance.State.Starting
        "running" -> RemoteApplicationInstance.State.Running
        "stopping" -> RemoteApplicationInstance.State.Stopping
        "stopped" -> RemoteApplicationInstance.State.Stopped
        else -> RemoteApplicationInstance.State.Failed
    },
    reconnectable = data.optBoolean("reconnectable", false),
)

internal fun parseTicket(data: JSONObject, inherited: ConnectionTicket? = null): AccountResult<ConnectionTicket> {
    val permissionsJson = data.optJSONArray("permissions") ?: JSONArray()
    val permissions = buildSet {
        repeat(permissionsJson.length()) { index -> add(permissionsJson.getString(index)) }
    }
    val parsed = ConnectionTicket(
        ticket = data.getString("ticket"),
        renewalToken = data.getString("renewal_token"),
        launchUrl = data.optString("launch_url").ifBlank { inherited?.launchUrl.orEmpty() },
        expiresAtEpochMillis = data.getLong("expires_at"),
        logicalSessionId = data.getString("logical_session_id"),
        streamId = data.getString("stream_id"),
        joinMode = if (data.optString("join_mode") == "observe") JoinMode.Observe else JoinMode.Control,
        permissions = permissions,
        rtcIceConfigJson = data.optJSONObject("rtc_ice_config")?.toString().orEmpty(),
        relayHost = data.optString("relay_host").ifBlank { inherited?.relayHost.orEmpty() },
        relayPort = data.optInt("relay_port").takeIf { it in 1..65535 } ?: inherited?.relayPort ?: 0,
        signalDeviceId = data.optString("signal_device_id").ifBlank { inherited?.signalDeviceId.orEmpty() },
    )
    if (parsed.ticket.isBlank() || parsed.renewalToken.isBlank() || parsed.launchUrl.isBlank() || parsed.streamId.isBlank()) {
        return AccountResult.Failure(AccountFailure.InvalidResponse)
    }
    if (inherited != null && (parsed.logicalSessionId != inherited.logicalSessionId || parsed.streamId != inherited.streamId)) {
        return AccountResult.Failure(AccountFailure.InvalidResponse)
    }
    return AccountResult.Success(parsed)
}
