package yun.pixels.client.core.network

import java.net.URI
import java.time.Instant
import java.util.UUID
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.update.AndroidUpdateArtifact
import yun.pixels.client.core.domain.update.AndroidUpdateRelease
import yun.pixels.client.core.domain.update.AndroidUpdateRepository
import org.json.JSONObject

class AndroidReleaseIdentity private constructor(
    internal val distribution: String,
    internal val releaseNamespace: String,
    internal val oemId: String?,
) {
    companion object {
        fun create(distribution: String, releaseNamespace: String, oemId: String?): AndroidReleaseIdentity? {
            val normalizedOemId = oemId?.takeIf(String::isNotEmpty)
            val validDomain = when (distribution) {
                "official" -> releaseNamespace == "pixels.official" && normalizedOemId == null
                "customer" -> releaseNamespace == "pixels.customer" && normalizedOemId == null
                "oem" -> normalizedOemId?.let { candidate ->
                    validOemId(candidate) && releaseNamespace == "oem.$candidate"
                } == true
                else -> false
            }
            return validDomain.takeIf { it }?.let { AndroidReleaseIdentity(distribution, releaseNamespace, normalizedOemId) }
        }
    }
}

interface ConsoleUpdateApi {
    suspend fun latestAndroidUpdate(session: AccountSession): AccountResult<AndroidUpdateRelease>
}

class ConsoleAndroidUpdateRepository(
    private val api: ConsoleUpdateApi,
    private val sessions: ConsoleSessionCoordinator,
) : AndroidUpdateRepository {
    override suspend fun latest(): AccountResult<AndroidUpdateRelease> {
        val session = sessions.currentUserSession()
            ?: return AccountResult.Failure(AccountFailure.AuthenticationRequired)
        return api.latestAndroidUpdate(session)
    }
}

internal fun parseAndroidUpdateRelease(
    payload: JSONObject,
    expectedIdentity: AndroidReleaseIdentity,
): AccountResult<AndroidUpdateRelease> {
    if (payload.fieldNames() != RELEASE_KEYS || payload.optString("state") != "approved") return invalidUpdateResponse()
    val releaseId = payload.strictUuid("id") ?: return invalidUpdateResponse()
    val publicationSha256 = payload.strictDigest("repository_publication_sha256") ?: return invalidUpdateResponse()
    val rootVersion = payload.strictPositiveLong("repository_root_version") ?: return invalidUpdateResponse()
    val revision = payload.strictPositiveLong("revision") ?: return invalidUpdateResponse()
    val createdAt = payload.strictInstantMillis("created_at") ?: return invalidUpdateResponse()
    val updatedAt = payload.strictInstantMillis("updated_at") ?: return invalidUpdateResponse()
    if (updatedAt < createdAt) return invalidUpdateResponse()
    val artifactPayload = payload.optJSONObject("artifact") ?: return invalidUpdateResponse()
    val artifact = parseArtifact(artifactPayload, expectedIdentity) ?: return invalidUpdateResponse()
    return AccountResult.Success(
        AndroidUpdateRelease(
            releaseId = releaseId,
            repositoryPublicationSha256 = publicationSha256,
            repositoryRootVersion = rootVersion,
            revision = revision,
            createdAtEpochMillis = createdAt,
            updatedAtEpochMillis = updatedAt,
            artifact = artifact,
        ),
    )
}

private fun parseArtifact(payload: JSONObject, expectedIdentity: AndroidReleaseIdentity): AndroidUpdateArtifact? {
    if (payload.fieldNames() != ARTIFACT_KEYS) return null
    val target = payload.optJSONObject("target") ?: return null
    if (target.fieldNames() != TARGET_KEYS || !target.matches(expectedIdentity)) return null
    val buildNumber = payload.strictPositiveLong("build_number") ?: return null
    val version = payload.strictText("version", 64) ?: return null
    val metadataBaseUrl = payload.strictRepositoryBaseUrl("metadata_base_url") ?: return null
    val targetsBaseUrl = payload.strictRepositoryBaseUrl("targets_base_url") ?: return null
    val targetName = payload.strictText("target_name", 512) ?: return null
    if (!matchesImmutableTargetName(targetName, expectedIdentity, buildNumber)) return null
    val sha256 = payload.strictDigest("sha256") ?: return null
    val platformSignerSha256 = payload.strictDigest("platform_signer_sha256") ?: return null
    val sizeBytes = payload.strictPositiveLong("size_bytes")?.takeIf { it <= MAXIMUM_ARTIFACT_BYTES } ?: return null
    return AndroidUpdateArtifact(
        buildNumber,
        version,
        metadataBaseUrl,
        targetsBaseUrl,
        targetName,
        sha256,
        platformSignerSha256,
        sizeBytes,
    )
}

private fun JSONObject.matches(expectedIdentity: AndroidReleaseIdentity): Boolean {
    val actualOemId = when {
        !has("oem_id") -> return false
        isNull("oem_id") -> null
        opt("oem_id") is String -> strictText("oem_id", 32) ?: return false
        else -> return false
    }
    return strictText("product", 32) == "android" &&
        strictText("distribution", 32) == expectedIdentity.distribution &&
        strictText("release_namespace", 64) == expectedIdentity.releaseNamespace &&
        actualOemId == expectedIdentity.oemId &&
        strictText("channel", 32) == "stable" &&
        strictText("os", 32) == "android" &&
        strictText("architecture", 32) == "aarch64"
}

private fun matchesImmutableTargetName(targetName: String, identity: AndroidReleaseIdentity, buildNumber: Long): Boolean {
    val expectedComponents = buildList {
        add("android")
        add("android")
        add(identity.distribution)
        identity.oemId?.let(::add)
        add("stable")
        add("aarch64")
        add(buildNumber.toString())
    }
    val actualComponents = targetName.split('/')
    if (actualComponents.size != expectedComponents.size + 1 || actualComponents.dropLast(1) != expectedComponents) return false
    val fileName = actualComponents.last()
    return fileName.isNotEmpty() && fileName !in setOf(".", "..") && fileName.none { character ->
        character == '\\' || character.isWhitespace() || character.isISOControl()
    }
}

private fun JSONObject.strictUuid(name: String): String? {
    val wireValue = strictText(name, 36) ?: return null
    return runCatching { UUID.fromString(wireValue).toString() }.getOrNull()?.takeIf { it == wireValue }
}

private fun JSONObject.strictDigest(name: String): String? = strictText(name, 64)?.takeIf { value ->
    value.length == 64 && value.all { character -> character in '0'..'9' || character in 'a'..'f' }
}

private fun JSONObject.strictPositiveLong(name: String): Long? {
    val number = opt(name)
    if (!has(name) || isNull(name) || number !is Int && number !is Long) return null
    return number.toLong().takeIf { it > 0 }
}

private fun JSONObject.strictText(name: String, maximumLength: Int): String? {
    if (!has(name) || isNull(name) || opt(name) !is String) return null
    return getString(name).takeIf { value ->
        value.isNotBlank() && value == value.trim() && value.length <= maximumLength && value.none(Char::isISOControl)
    }
}

private fun JSONObject.strictInstantMillis(name: String): Long? =
    strictText(name, 64)?.let { value -> runCatching { Instant.parse(value).toEpochMilli() }.getOrNull() }

private fun JSONObject.strictRepositoryBaseUrl(name: String): String? = strictText(name, 2_048)?.takeIf { value ->
    runCatching {
        val uri = URI(value)
        uri.scheme.equals("https", ignoreCase = true) && !uri.host.isNullOrBlank() && uri.userInfo == null &&
            uri.query == null && uri.fragment == null && uri.path.endsWith('/') && (uri.port == -1 || uri.port in 1..65_535)
    }.getOrDefault(false)
}

private fun JSONObject.fieldNames(): Set<String> = keys().asSequence().toSet()

private fun validOemId(value: String): Boolean =
    value.length in 3..32 &&
        value.firstOrNull()?.let { character -> character.isLowerCaseOrDigit() } == true &&
        value.lastOrNull()?.let { character -> character.isLowerCaseOrDigit() } == true &&
        value.none { character -> !character.isLowerCaseOrDigit() && character != '-' } &&
        !value.contains("--") &&
        value !in setOf("pixels", "official", "customer", "oem")

private fun Char.isLowerCaseOrDigit(): Boolean = this in 'a'..'z' || this in '0'..'9'

private fun <T> invalidUpdateResponse(): AccountResult<T> = AccountResult.Failure(AccountFailure.InvalidResponse)

private val RELEASE_KEYS = setOf(
    "id",
    "artifact",
    "repository_publication_sha256",
    "repository_root_version",
    "state",
    "revision",
    "created_at",
    "updated_at",
)
private val ARTIFACT_KEYS = setOf(
    "target",
    "build_number",
    "version",
    "metadata_base_url",
    "targets_base_url",
    "target_name",
    "sha256",
    "platform_signer_sha256",
    "size_bytes",
)
private val TARGET_KEYS = setOf("product", "distribution", "release_namespace", "oem_id", "channel", "os", "architecture")
private const val MAXIMUM_ARTIFACT_BYTES = 1L shl 40
