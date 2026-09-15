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
import yun.pixels.client.core.domain.account.AccountConnection
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationAccess
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.RemoteApplicationType
import java.net.HttpURLConnection
import java.net.URI
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import javax.net.ssl.HttpsURLConnection

interface ConsoleAccountApi {
    suspend fun testEndpoint(endpointInput: String): AccountResult<ConsoleEndpoint>

    suspend fun guestSession(endpoint: ConsoleEndpoint, clientNonce: String): AccountResult<GuestSession>

    suspend fun register(endpoint: ConsoleEndpoint, guestToken: String, username: String, password: String): AccountResult<AccountProfile>

    suspend fun login(endpointInput: String, username: String, password: String): AccountResult<AccountSession>

    suspend fun logout(session: AccountSession): AccountResult<Unit>

    suspend fun devices(session: AccountSession): AccountResult<List<AccountDevice>>

    suspend fun resolveConnection(session: AccountSession, deviceId: String): AccountResult<AccountConnection>
}

class ConsoleApiClient(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ConsoleAccountApi, ConsoleApplicationApi {
    override suspend fun testEndpoint(endpointInput: String): AccountResult<ConsoleEndpoint> = withContext(ioDispatcher) {
        val endpoint = normalizeEndpoint(endpointInput)
            ?: return@withContext AccountResult.Failure(AccountFailure.InvalidEndpoint)
        request(endpoint, "/api/v1/public/apps", "GET")?.toAccountResult { data ->
            if (data is JSONArray) AccountResult.Success(endpoint) else AccountResult.Failure(AccountFailure.InvalidResponse)
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun guestSession(endpoint: ConsoleEndpoint, clientNonce: String): AccountResult<GuestSession> = withContext(ioDispatcher) {
        val body = JSONObject().put("client_nonce", clientNonce).put("client_type", "android")
        request(endpoint, "/api/v1/session/guest", "POST", body = body)?.toAccountResult { data ->
            val payload = data as JSONObject
            val token = payload.optString("access_token").takeIf(String::isNotBlank)
                ?: return@toAccountResult AccountResult.Failure(AccountFailure.InvalidResponse)
            AccountResult.Success(GuestSession(endpoint, token, payload.getLong("expires_at")))
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun register(
        endpoint: ConsoleEndpoint,
        guestToken: String,
        username: String,
        password: String,
    ): AccountResult<AccountProfile> = withContext(ioDispatcher) {
        val body = JSONObject().put("username", username.trim()).put("password", password)
        request(endpoint, "/api/v1/user/register", "POST", guestToken, body)?.toAccountResult { data ->
            AccountResult.Success(parseProfile(data as JSONObject))
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun login(endpointInput: String, username: String, password: String): AccountResult<AccountSession> = withContext(ioDispatcher) {
        val endpoint = normalizeEndpoint(endpointInput)
            ?: return@withContext AccountResult.Failure(AccountFailure.InvalidEndpoint)
        val body = JSONObject()
            .put("username", username.trim())
            .put("password", password)
            .put("client_type", "android")
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

    override suspend fun resolveConnection(session: AccountSession, deviceId: String): AccountResult<AccountConnection> = withContext(ioDispatcher) {
        val encodedDeviceId = URLEncoder.encode(deviceId, StandardCharsets.UTF_8.name()).replace("+", "%20")
        request(
            session.endpoint,
            "/api/v1/user/devices/$encodedDeviceId/native-connection",
            "POST",
            session.accessToken,
            JSONObject(),
        )?.toAccountResult { data -> parseAccountConnection(data as JSONObject) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/v1/user/apps", "GET", session.accessToken)?.toAccountResult { data ->
            AccountResult.Success(parseApplications(data as JSONArray))
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun publicApplications(endpoint: ConsoleEndpoint): AccountResult<List<RemoteApplication>> = withContext(ioDispatcher) {
        request(endpoint, "/api/v1/public/apps", "GET")?.toAccountResult { data ->
            AccountResult.Success(parseApplications(data as JSONArray))
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun guestInstances(session: GuestSession): AccountResult<List<RemoteApplicationInstance>> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/v1/public/instances", "GET", session.accessToken)?.toAccountResult { data ->
            val rows = data as JSONArray
            AccountResult.Success(buildList {
                repeat(rows.length()) { index -> add(parseApplicationInstance(rows.getJSONObject(index))) }
            })
        } ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun startGuestApplication(
        session: GuestSession,
        appId: String,
        clientNonce: String,
    ): AccountResult<RemoteApplicationInstance> = withContext(ioDispatcher) {
        val body = JSONObject().put("client_nonce", clientNonce)
        request(session.endpoint, "/api/v1/public/apps/${encodePathSegment(appId)}/start", "POST", session.accessToken, body)
            ?.toAccountResult { data -> AccountResult.Success(parseApplicationInstance(data as JSONObject)) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun stopGuestApplication(session: GuestSession, instanceId: String): AccountResult<Unit> = withContext(ioDispatcher) {
        val body = JSONObject().put("reason", "stopped from Pixels Android")
        request(session.endpoint, "/api/v1/public/instances/${encodePathSegment(instanceId)}/stop", "POST", session.accessToken, body)
            ?.toAccountResult { AccountResult.Success(Unit) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun resolveGuestApplicationConnection(
        session: GuestSession,
        instanceId: String,
    ): AccountResult<AccountConnection> = withContext(ioDispatcher) {
        request(
            session.endpoint,
            "/api/v1/public/instances/${encodePathSegment(instanceId)}/native-connection",
            "POST",
            session.accessToken,
            JSONObject().put("view_only", false).put("client_capability", "android-native-v1"),
        )?.toAccountResult { data -> parseAccountConnection(data as JSONObject) }
            ?: AccountResult.Failure(AccountFailure.NetworkUnavailable)
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

    override suspend fun resolveApplicationConnection(
        session: AccountSession,
        instanceId: String,
    ): AccountResult<AccountConnection> = withContext(ioDispatcher) {
        val encodedInstanceId = encodePathSegment(instanceId)
        request(
            session.endpoint,
            "/api/v1/user/instances/$encodedInstanceId/native-connection",
            "POST",
            session.accessToken,
            JSONObject().put("view_only", false).put("client_capability", "android-native-v1"),
        )?.toAccountResult { data -> parseAccountConnection(data as JSONObject) }
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
    val envelope = runCatching { JSONObject(body) }.getOrNull()
    val error = envelope?.optString("error").orEmpty()
    val businessCode = envelope?.optInt("code")
    return when (error) {
        "AUTH_INVALID_CREDENTIALS" -> AccountFailure.InvalidCredentials
        "AUTH_REQUIRED" -> AccountFailure.AuthenticationRequired
        "SUBJECT_FORBIDDEN" -> AccountFailure.Forbidden
        "RATE_LIMITED" -> AccountFailure.RateLimited
        "QUOTA_EXCEEDED" -> AccountFailure.QuotaExceeded
        "USERNAME_CONFLICT", "USER_ALREADY_EXISTS" -> AccountFailure.UsernameConflict
        "APPLICATION_INSTANCE_BUSY", "INSTANCE_BUSY" -> AccountFailure.InstanceBusy
        "DEVICE_OFFLINE" -> AccountFailure.DeviceOffline
        "RESOURCE_NOT_FOUND" -> AccountFailure.NotFound
        else -> when (businessCode) {
            608 -> AccountFailure.UsernameConflict
            638 -> AccountFailure.RateLimited
            639 -> AccountFailure.QuotaExceeded
            else -> status.toAccountFailure()
        }
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
    val accessToken = data.getString("access_token").takeIf(String::isNotBlank)
        ?: return AccountResult.Failure(AccountFailure.InvalidResponse)
    val profile = parseProfile(data.getJSONObject("profile"))
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
                type = item.optString("app_type").toApplicationType(),
                access = when (item.optString("access_mode").lowercase()) {
                    "public" -> RemoteApplicationAccess.Public
                    "acl" -> RemoteApplicationAccess.Acl
                    else -> RemoteApplicationAccess.Unknown
                },
                version = item.optLong("version"),
                runningInstance = running?.let(::parseApplicationInstance),
            ),
        )
    }
}

private fun parseProfile(data: JSONObject): AccountProfile = AccountProfile(
    userId = data.getString("uid"),
    username = data.getString("username"),
    avatarPath = data.optString("avatar_path").takeIf(String::isNotBlank),
    mustChangePassword = data.optBoolean("must_change_password", false),
)

private fun parseApplicationInstance(data: JSONObject): RemoteApplicationInstance = RemoteApplicationInstance(
    instanceId = data.getString("instance_id"),
    appId = data.optString("app_id"),
    state = when (data.optString("state").lowercase()) {
        "starting" -> RemoteApplicationInstance.State.Starting
        "running" -> RemoteApplicationInstance.State.Running
        "stopping" -> RemoteApplicationInstance.State.Stopping
        "stopped" -> RemoteApplicationInstance.State.Stopped
        else -> RemoteApplicationInstance.State.Failed
    },
    reconnectable = data.optBoolean("reconnectable", false),
)

internal fun parseAccountConnection(data: JSONObject): AccountResult<AccountConnection> {
    val parsed = AccountConnection(
        host = data.optString("host"),
        port = data.optInt("port"),
        deviceId = data.optString("device_id"),
        instanceId = data.optString("instance_id"),
        passwordHash = data.optString("password_hash"),
        relayHost = data.optString("relay_host"),
        relayPort = data.optInt("relay_port"),
        signalDeviceId = data.optString("signal_device_id"),
        appType = data.optString("app_type").takeIf(String::isNotBlank)?.toApplicationType(),
    )
    if (parsed.host.isBlank() || parsed.port !in 1..65535 || parsed.deviceId.isBlank() || parsed.passwordHash.isBlank()) {
        return AccountResult.Failure(AccountFailure.InvalidResponse)
    }
    return AccountResult.Success(parsed)
}

private fun String.toApplicationType(): RemoteApplicationType = when (lowercase()) {
    "game-hook", "game_hook" -> RemoteApplicationType.GameHook
    "webview" -> RemoteApplicationType.WebView
    "rdp" -> RemoteApplicationType.Rdp
    else -> RemoteApplicationType.Unknown
}
