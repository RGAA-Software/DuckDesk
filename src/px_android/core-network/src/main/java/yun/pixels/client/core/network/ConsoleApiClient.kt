package yun.pixels.client.core.network

import java.net.URI
import java.net.URLEncoder
import java.nio.charset.StandardCharsets
import java.time.Instant
import java.util.UUID
import javax.net.ssl.HttpsURLConnection
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
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.GuestSession
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationAccess
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.RemoteApplicationType
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.account.ResourceRelayEndpoint

interface ConsoleAccountApi {
    suspend fun testEndpoint(endpointInput: String): AccountResult<ConsoleEndpoint>
    suspend fun guestSession(endpoint: ConsoleEndpoint): AccountResult<GuestSession>
    suspend fun register(endpoint: ConsoleEndpoint, username: String, password: String): AccountResult<AccountProfile>
    suspend fun login(endpointInput: String, username: String, password: String): AccountResult<AccountSession>
    suspend fun logout(session: AccountSession): AccountResult<Unit>
    suspend fun devices(session: AccountSession): AccountResult<List<AccountDevice>>
    suspend fun resolveConnection(session: AccountSession, deviceId: String): AccountResult<ResourceConnection>
}

class ConsoleApiClient(
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : ConsoleAccountApi, ConsoleApplicationApi {
    override suspend fun testEndpoint(endpointInput: String): AccountResult<ConsoleEndpoint> = withContext(ioDispatcher) {
        val endpoint = normalizeEndpoint(endpointInput) ?: return@withContext failure(AccountFailure.InvalidEndpoint)
        val response = request(endpoint, "/health/ready", "GET") ?: return@withContext failure(AccountFailure.NetworkUnavailable)
        if (response.status in 200..299) AccountResult.Success(endpoint) else failure(accountFailure(response))
    }

    override suspend fun guestSession(endpoint: ConsoleEndpoint): AccountResult<GuestSession> = withContext(ioDispatcher) {
        request(endpoint, "/api/console/guest-sessions", "POST", body = JSONObject())?.parseObject { payload ->
            val token = payload.requiredString("token") ?: return@parseObject invalidResponse()
            val session = payload.optJSONObject("session") ?: return@parseObject invalidResponse()
            val expiresAt = session.requiredInstantMillis("expires_at") ?: return@parseObject invalidResponse()
            AccountResult.Success(GuestSession(endpoint, token, expiresAt))
        } ?: failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun register(endpoint: ConsoleEndpoint, username: String, password: String): AccountResult<AccountProfile> =
        withContext(ioDispatcher) {
            request(
                endpoint,
                "/api/console/accounts",
                "POST",
                body = JSONObject().put("username", username.trim()).put("password", password),
            )?.parseObject { AccountResult.Success(parseProfile(it)) } ?: failure(AccountFailure.NetworkUnavailable)
        }

    override suspend fun login(endpointInput: String, username: String, password: String): AccountResult<AccountSession> =
        withContext(ioDispatcher) {
            val endpoint = normalizeEndpoint(endpointInput) ?: return@withContext failure(AccountFailure.InvalidEndpoint)
            val response = request(
                endpoint,
                "/api/console/sessions",
                "POST",
                body = JSONObject().put("username", username.trim()).put("password", password),
            ) ?: return@withContext failure(AccountFailure.NetworkUnavailable)
            if (response.status == 401) return@withContext failure(AccountFailure.InvalidCredentials)
            response.parseObject { parseLogin(endpoint, it) }
        }

    override suspend fun logout(session: AccountSession): AccountResult<Unit> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/console/session", "DELETE", session.accessToken)?.successWithoutBody()
            ?: failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun devices(session: AccountSession): AccountResult<List<AccountDevice>> = withContext(ioDispatcher) {
        request(session.endpoint, "/api/console/devices?limit=100", "GET", session.accessToken)?.parseArray(::parseDevices)
            ?: failure(AccountFailure.NetworkUnavailable)
    }

    override suspend fun resolveConnection(session: AccountSession, deviceId: String): AccountResult<ResourceConnection> =
        withContext(ioDispatcher) {
            resolveResourceConnection(
                session.endpoint,
                session.accessToken,
                SUBJECT_USER,
                JSONObject().put("kind", "desktop").put("device_id", deviceId),
                deviceId,
            )
        }

    override suspend fun applications(session: AccountSession): AccountResult<List<RemoteApplication>> = withContext(ioDispatcher) {
        catalogWithInstances(session.endpoint, session.accessToken, SUBJECT_USER, "/api/console/applications?limit=100")
    }

    override suspend fun publicApplications(session: GuestSession): AccountResult<List<RemoteApplication>> = withContext(ioDispatcher) {
        catalogWithInstances(session.endpoint, session.accessToken, SUBJECT_GUEST, "/api/console/guest/applications?limit=100")
    }

    override suspend fun startGuestApplication(
        session: GuestSession,
        appId: String,
        clientNonce: String,
    ): AccountResult<RemoteApplicationInstance> = withContext(ioDispatcher) {
        startApplicationRequest(session.endpoint, session.accessToken, SUBJECT_GUEST, appId, clientNonce)
    }

    override suspend fun stopGuestApplication(session: GuestSession, instanceId: String): AccountResult<Unit> = withContext(ioDispatcher) {
        stopApplicationRequest(session.endpoint, session.accessToken, SUBJECT_GUEST, instanceId)
    }

    override suspend fun resolveGuestApplicationConnection(
        session: GuestSession,
        appId: String,
        instanceId: String,
    ): AccountResult<ResourceConnection> = withContext(ioDispatcher) {
        resolveCloudApplicationConnection(session.endpoint, session.accessToken, SUBJECT_GUEST, appId, instanceId)
    }

    override suspend fun startApplication(
        session: AccountSession,
        appId: String,
        clientNonce: String,
    ): AccountResult<RemoteApplicationInstance> = withContext(ioDispatcher) {
        startApplicationRequest(session.endpoint, session.accessToken, SUBJECT_USER, appId, clientNonce)
    }

    override suspend fun stopApplication(session: AccountSession, instanceId: String): AccountResult<Unit> = withContext(ioDispatcher) {
        stopApplicationRequest(session.endpoint, session.accessToken, SUBJECT_USER, instanceId)
    }

    override suspend fun resolveApplicationConnection(
        session: AccountSession,
        appId: String,
        instanceId: String,
    ): AccountResult<ResourceConnection> = withContext(ioDispatcher) {
        resolveCloudApplicationConnection(session.endpoint, session.accessToken, SUBJECT_USER, appId, instanceId)
    }

    private fun catalogWithInstances(
        endpoint: ConsoleEndpoint,
        accessToken: String,
        subjectKind: String,
        catalogPath: String,
    ): AccountResult<List<RemoteApplication>> {
        val catalog = request(endpoint, catalogPath, "GET", accessToken)?.parseArray(::parseApplications)
            ?: return failure(AccountFailure.NetworkUnavailable)
        val applications = when (catalog) {
            is AccountResult.Success -> catalog.value
            is AccountResult.Failure -> return catalog
        }
        val instances = ownedInstances(endpoint, accessToken, subjectKind)
        val instanceValues = when (instances) {
            is AccountResult.Success -> instances.value
            is AccountResult.Failure -> return instances
        }
        return AccountResult.Success(mergeInstances(applications, instanceValues))
    }

    private fun startApplicationRequest(
        endpoint: ConsoleEndpoint,
        accessToken: String,
        subjectKind: String,
        appId: String,
        clientNonce: String,
    ): AccountResult<RemoteApplicationInstance> {
        val requestId = runCatching { UUID.fromString(clientNonce).toString() }.getOrNull() ?: return invalidResponse()
        val body = JSONObject().put("request_id", requestId).put("application_id", appId).put("deployment_id", JSONObject.NULL)
        return request(endpoint, "/api/console/instances", "POST", accessToken, subjectKind, body)?.parseObject {
            AccountResult.Success(parseApplicationInstance(it))
        } ?: failure(AccountFailure.NetworkUnavailable)
    }

    private fun stopApplicationRequest(
        endpoint: ConsoleEndpoint,
        accessToken: String,
        subjectKind: String,
        instanceId: String,
    ): AccountResult<Unit> {
        val encodedId = encodePathSegment(instanceId)
        val current = request(endpoint, "/api/console/instances/$encodedId", "GET", accessToken, subjectKind)?.parseObject { payload ->
            payload.requiredLong("revision")?.let { AccountResult.Success(it) } ?: invalidResponse()
        } ?: return failure(AccountFailure.NetworkUnavailable)
        val revision = when (current) {
            is AccountResult.Success -> current.value
            is AccountResult.Failure -> return current
        }
        return request(
            endpoint,
            "/api/console/instances/$encodedId/stop",
            "POST",
            accessToken,
            subjectKind,
            JSONObject().put("revision", revision),
        )?.parseObject { AccountResult.Success(Unit) } ?: failure(AccountFailure.NetworkUnavailable)
    }

    private fun ownedInstances(
        endpoint: ConsoleEndpoint,
        accessToken: String,
        subjectKind: String,
    ): AccountResult<List<RemoteApplicationInstance>> =
        request(endpoint, "/api/console/instances?limit=100", "GET", accessToken, subjectKind)?.parseArray { rows ->
            AccountResult.Success(buildList {
                repeat(rows.length()) { index -> add(parseApplicationInstance(rows.getJSONObject(index))) }
            })
        } ?: failure(AccountFailure.NetworkUnavailable)

    private fun resolveCloudApplicationConnection(
        endpoint: ConsoleEndpoint,
        accessToken: String,
        subjectKind: String,
        appId: String,
        instanceId: String,
    ): AccountResult<ResourceConnection> = resolveResourceConnection(
        endpoint,
        accessToken,
        subjectKind,
        JSONObject().put("kind", "cloud_application").put("application_id", appId).put("instance_id", instanceId),
        instanceId,
    )

    private fun resolveResourceConnection(
        endpoint: ConsoleEndpoint,
        accessToken: String,
        subjectKind: String,
        target: JSONObject,
        remoteResourceId: String,
    ): AccountResult<ResourceConnection> {
        val openBody = JSONObject().put("request_id", UUID.randomUUID().toString()).put("target", target).put("access", "controller")
        val opened = request(endpoint, "/api/console/resource-sessions", "POST", accessToken, subjectKind, openBody)?.parseObject { payload ->
            val sessionId = payload.requiredString("id") ?: return@parseObject invalidResponse()
            val revision = payload.requiredLong("revision") ?: return@parseObject invalidResponse()
            AccountResult.Success(OpenedResourceSession(sessionId, revision))
        } ?: return failure(AccountFailure.NetworkUnavailable)
        val openedSession = when (opened) {
            is AccountResult.Success -> opened.value
            is AccountResult.Failure -> return opened
        }
        return request(
            endpoint,
            "/api/console/resource-sessions/${encodePathSegment(openedSession.sessionId)}/descriptor",
            "POST",
            accessToken,
            subjectKind,
            JSONObject().put("revision", openedSession.revision),
        )?.parseObject { parseResourceConnection(it, openedSession, target, remoteResourceId) }
            ?: failure(AccountFailure.NetworkUnavailable)
    }

    private fun request(
        endpoint: ConsoleEndpoint,
        path: String,
        method: String,
        bearerToken: String? = null,
        subjectKind: String? = null,
        body: JSONObject? = null,
    ): HttpResponse? {
        val connection = runCatching { URI(endpoint.baseUrl).resolve(path).toURL().openConnection() as HttpsURLConnection }.getOrNull()
            ?: return null
        return try {
            connection.requestMethod = method
            connection.connectTimeout = CONNECT_TIMEOUT_MILLIS
            connection.readTimeout = READ_TIMEOUT_MILLIS
            connection.instanceFollowRedirects = false
            connection.useCaches = false
            connection.setRequestProperty("Accept", "application/json")
            connection.setRequestProperty("User-Agent", "Pixels-Android/1")
            connection.setRequestProperty("X-Pixels-Client-Type", "android")
            bearerToken?.let { connection.setRequestProperty("Authorization", "Bearer $it") }
            subjectKind?.let { connection.setRequestProperty("X-Pixels-Subject-Kind", it) }
            if (body != null) {
                connection.doOutput = true
                connection.setRequestProperty("Content-Type", "application/json; charset=utf-8")
                connection.outputStream.use { it.write(body.toString().toByteArray(StandardCharsets.UTF_8)) }
            }
            val status = connection.responseCode
            val stream = if (status in 200..299) connection.inputStream else connection.errorStream
            HttpResponse(status, stream?.bufferedReader(StandardCharsets.UTF_8)?.use { it.readText() }.orEmpty())
        } catch (_: Exception) {
            null
        } finally {
            connection.disconnect()
        }
    }

    companion object {
        private const val CONNECT_TIMEOUT_MILLIS = 5_000
        private const val READ_TIMEOUT_MILLIS = 8_000
        private const val SUBJECT_USER = "user"
        private const val SUBJECT_GUEST = "guest"
    }
}

internal data class OpenedResourceSession(val sessionId: String, val revision: Long)
internal data class HttpResponse(val status: Int, val body: String)

internal fun normalizeEndpoint(input: String): ConsoleEndpoint? = runCatching {
    val normalized = input.trim().trimEnd('/')
    val uri = URI(normalized)
    if (!uri.scheme.equals("https", ignoreCase = true)) return null
    if (uri.host.isNullOrBlank() || uri.userInfo != null || uri.query != null || uri.fragment != null) return null
    if (!uri.path.isNullOrEmpty() || uri.port != -1 && uri.port !in 1..65535) return null
    ConsoleEndpoint(uri.toASCIIString())
}.getOrNull()

private inline fun <T> HttpResponse.parseObject(parse: (JSONObject) -> AccountResult<T>): AccountResult<T> {
    if (status !in 200..299) return failure(accountFailure(this))
    return runCatching { parse(JSONObject(body)) }.getOrElse { invalidResponse() }
}

private inline fun <T> HttpResponse.parseArray(parse: (JSONArray) -> AccountResult<T>): AccountResult<T> {
    if (status !in 200..299) return failure(accountFailure(this))
    return runCatching { parse(JSONArray(body)) }.getOrElse { invalidResponse() }
}

private fun HttpResponse.successWithoutBody(): AccountResult<Unit> =
    if (status in 200..299) AccountResult.Success(Unit) else failure(accountFailure(this))

internal fun accountFailure(response: HttpResponse): AccountFailure {
    val code = runCatching { JSONObject(response.body).optString("code") }.getOrDefault("")
    return when (code) {
        "unauthorized" -> AccountFailure.AuthenticationRequired
        "rejected" -> AccountFailure.Forbidden
        "rate_limited" -> AccountFailure.RateLimited
        "not_found" -> AccountFailure.NotFound
        "conflict" -> AccountFailure.InstanceBusy
        "unavailable", "internal" -> AccountFailure.ServerError
        "invalid_input" -> AccountFailure.InvalidResponse
        else -> when (response.status) {
            401 -> AccountFailure.AuthenticationRequired
            403 -> AccountFailure.Forbidden
            404 -> AccountFailure.NotFound
            409 -> AccountFailure.InstanceBusy
            429 -> AccountFailure.RateLimited
            in 500..599 -> AccountFailure.ServerError
            else -> AccountFailure.InvalidResponse
        }
    }
}

internal fun parseLogin(endpoint: ConsoleEndpoint, payload: JSONObject): AccountResult<AccountSession> {
    val accessToken = payload.requiredString("token") ?: return invalidResponse()
    val profile = payload.optJSONObject("profile") ?: return invalidResponse()
    val expiresAt = payload.requiredInstantMillis("expires_at") ?: return invalidResponse()
    return AccountResult.Success(AccountSession(endpoint, parseProfile(profile), accessToken, expiresAt))
}

private fun parseProfile(payload: JSONObject): AccountProfile = AccountProfile(
    userId = payload.getString("id"),
    username = payload.getString("username"),
    avatarPath = payload.optionalString("avatar_url"),
    mustChangePassword = false,
)

private fun parseDevices(rows: JSONArray): AccountResult<List<AccountDevice>> = AccountResult.Success(buildList {
    repeat(rows.length()) { index ->
        val item = rows.getJSONObject(index)
        add(AccountDevice(item.getString("id"), item.optString("name").ifBlank { item.getString("public_code") }, !item.optBoolean("disabled"), null))
    }
})

private fun parseApplications(rows: JSONArray): AccountResult<List<RemoteApplication>> = AccountResult.Success(buildList {
    repeat(rows.length()) { index ->
        val item = rows.getJSONObject(index)
        val access = when (item.optString("access_mode").lowercase()) {
            "public" -> RemoteApplicationAccess.Public
            "acl" -> RemoteApplicationAccess.Acl
            else -> RemoteApplicationAccess.Unknown
        }
        add(
            RemoteApplication(
                item.getString("id"),
                item.optString("name").ifBlank { item.getString("id") },
                "",
                item.optString("kind").toApplicationType(),
                access,
                item.optLong("revision"),
                null,
            ),
        )
    }
})

private fun parseApplicationInstance(payload: JSONObject): RemoteApplicationInstance {
    val stateName = payload.optString("state").lowercase()
    val state = when (stateName) {
        "reserved", "starting" -> RemoteApplicationInstance.State.Starting
        "running" -> RemoteApplicationInstance.State.Running
        "stop_requested", "stopping" -> RemoteApplicationInstance.State.Stopping
        "stopped" -> RemoteApplicationInstance.State.Stopped
        else -> RemoteApplicationInstance.State.Failed
    }
    return RemoteApplicationInstance(payload.getString("id"), payload.getString("application_id"), state, stateName == "running")
}

internal fun parseResourceConnection(
    payload: JSONObject,
    opened: OpenedResourceSession,
    expectedTarget: JSONObject,
    remoteResourceId: String,
): AccountResult<ResourceConnection> {
    val descriptor = payload.optJSONObject("descriptor") ?: return invalidResponse()
    val session = descriptor.optJSONObject("session") ?: return invalidResponse()
    val sessionId = session.requiredString("id") ?: return invalidResponse()
    val revision = session.requiredLong("revision") ?: return invalidResponse()
    val token = payload.requiredString("token") ?: return invalidResponse()
    val host = descriptor.requiredString("host") ?: return invalidResponse()
    val port = descriptor.optInt("port")
    val transport = descriptor.requiredString("transport") ?: return invalidResponse()
    val expiresAt = descriptor.requiredInstantMillis("expires_at") ?: return invalidResponse()
    val actualTarget = session.optJSONObject("target") ?: return invalidResponse()
    val relay = when (val relayPayload = payload.optJSONObject("relay")) {
        null -> null
        else -> {
            val relayHost = relayPayload.requiredString("host") ?: return invalidResponse()
            val relayPort = relayPayload.optInt("port")
            val relayAdmissionTicket = relayPayload.requiredString("admission_ticket") ?: return invalidResponse()
            if (relayPort !in 1..65535 || relayAdmissionTicket.length !in 64..256) return invalidResponse()
            ResourceRelayEndpoint(relayHost, relayPort, relayAdmissionTicket)
        }
    }
    if (
        sessionId != opened.sessionId ||
        revision < opened.revision ||
        session.optString("client_type") != "android" ||
        session.optString("access_role") != "controller" ||
        session.optString("state") !in setOf("pending", "connected") ||
        !sameResourceTarget(expectedTarget, actualTarget) ||
        port !in 1..65535 ||
        transport != "native"
    ) {
        return invalidResponse()
    }
    return AccountResult.Success(ResourceConnection(host, port, remoteResourceId, sessionId, revision, token, transport, expiresAt, relay))
}

private fun sameResourceTarget(expected: JSONObject, actual: JSONObject): Boolean = when (expected.optString("kind")) {
    "desktop" -> actual.optString("kind") == "desktop" && actual.optString("device_id") == expected.optString("device_id")
    "cloud_application" ->
        actual.optString("kind") == "cloud_application" &&
            actual.optString("application_id") == expected.optString("application_id") &&
            actual.optString("instance_id") == expected.optString("instance_id")
    else -> false
}

internal fun mergeInstances(
    applications: List<RemoteApplication>,
    instances: List<RemoteApplicationInstance>,
): List<RemoteApplication> {
    val activeStates = setOf(
        RemoteApplicationInstance.State.Starting,
        RemoteApplicationInstance.State.Running,
        RemoteApplicationInstance.State.Stopping,
    )
    val activeByApplication = instances.filter { it.state in activeStates }.associateBy(RemoteApplicationInstance::appId)
    return applications.map { it.copy(runningInstance = activeByApplication[it.appId]) }
}

private fun JSONObject.requiredString(name: String): String? = optString(name).takeIf(String::isNotBlank)
private fun JSONObject.optionalString(name: String): String? =
    if (has(name) && !isNull(name)) optString(name).takeIf(String::isNotBlank) else null
private fun JSONObject.requiredLong(name: String): Long? = if (has(name) && !isNull(name)) optLong(name) else null
private fun JSONObject.requiredInstantMillis(name: String): Long? =
    requiredString(name)?.let { runCatching { Instant.parse(it).toEpochMilli() }.getOrNull() }

private fun encodePathSegment(value: String): String = URLEncoder.encode(value, StandardCharsets.UTF_8.name()).replace("+", "%20")

private fun String.toApplicationType(): RemoteApplicationType = when (lowercase()) {
    "game_hook" -> RemoteApplicationType.GameHook
    "webview" -> RemoteApplicationType.WebView
    "rdp" -> RemoteApplicationType.Rdp
    else -> RemoteApplicationType.Unknown
}

private fun <T> invalidResponse(): AccountResult<T> = failure(AccountFailure.InvalidResponse)
private fun <T> failure(reason: AccountFailure): AccountResult<T> = AccountResult.Failure(reason)
