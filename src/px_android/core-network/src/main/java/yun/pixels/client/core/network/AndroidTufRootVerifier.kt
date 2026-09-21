package yun.pixels.client.core.network

import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.time.Instant
import org.json.JSONArray
import org.json.JSONObject

class AndroidTufTrustConfiguration private constructor(
    internal val initialRootBytes: ByteArray,
    internal val initialRootVersion: Long,
    internal val initialRootExpiresAtEpochSeconds: Long,
) {
    companion object {
        fun create(initialRootBytes: ByteArray, nowEpochSeconds: Long = System.currentTimeMillis() / 1_000): AndroidTufTrustConfiguration? =
            create(initialRootBytes, nowEpochSeconds, JcaEd25519Verifier.android())

        internal fun create(
            initialRootBytes: ByteArray,
            nowEpochSeconds: Long,
            signatureVerifier: DeploymentSignatureVerifier,
        ): AndroidTufTrustConfiguration? {
            val verifiedRoot = AndroidTufRootVerifier(signatureVerifier).verifyInitialRoot(initialRootBytes, nowEpochSeconds) ?: return null
            return AndroidTufTrustConfiguration(initialRootBytes.copyOf(), verifiedRoot.version, verifiedRoot.expiresAtEpochSeconds)
        }
    }
}

internal data class VerifiedTufRoot(
    val version: Long,
    val expiresAtEpochSeconds: Long,
)

private data class TufRole(
    val keyIds: Set<String>,
    val threshold: Int,
)

internal class AndroidTufRootVerifier(
    private val signatureVerifier: DeploymentSignatureVerifier,
) {
    fun verifyInitialRoot(rootBytes: ByteArray, nowEpochSeconds: Long): VerifiedTufRoot? = runCatching {
        if (rootBytes.isEmpty() || rootBytes.size > MAXIMUM_ROOT_BYTES || nowEpochSeconds < 0) return null
        val envelope = JSONObject(String(rootBytes, StandardCharsets.UTF_8))
        if (!envelope.hasExactly("signed", "signatures")) return null
        val signed = envelope.optJSONObject("signed") ?: return null
        if (!signed.hasExactly("_type", "spec_version", "consistent_snapshot", "version", "expires", "keys", "roles")) return null
        if (signed.strictString("_type") != "root" || signed.strictString("spec_version") != "1.0.0") return null
        if (signed.opt("consistent_snapshot") !is Boolean) return null
        val version = signed.strictPositiveLong("version") ?: return null
        val expiresAt = signed.strictString("expires")?.let { Instant.parse(it).epochSecond } ?: return null
        if (expiresAt <= nowEpochSeconds) return null

        val keysPayload = signed.optJSONObject("keys") ?: return null
        if (keysPayload.length() !in MINIMUM_KEY_COUNT..MAXIMUM_KEY_COUNT) return null
        val publicKeys = mutableMapOf<String, ByteArray>()
        keysPayload.keys().asSequence().forEach { keyId ->
            val keyPayload = keysPayload.optJSONObject(keyId) ?: return null
            if (!keyPayload.hasExactly("keytype", "scheme", "keyval")) return null
            if (keyPayload.strictString("keytype") != "ed25519" || keyPayload.strictString("scheme") != "ed25519") return null
            val keyValue = keyPayload.optJSONObject("keyval") ?: return null
            if (!keyValue.hasExactly("public")) return null
            val publicKey = keyValue.strictString("public")?.decodeCanonicalHex(PUBLIC_KEY_BYTES) ?: return null
            if (keyId != canonicalTufJson(keyPayload).sha256Hex() || publicKeys.put(keyId, publicKey) != null) return null
        }

        val rolesPayload = signed.optJSONObject("roles") ?: return null
        if (rolesPayload.keys().asSequence().toSet() != REQUIRED_ROLES) return null
        val roles = REQUIRED_ROLES.associateWith { roleName ->
            parseRole(rolesPayload.optJSONObject(roleName), publicKeys.keys) ?: return null
        }
        val rootRole = roles.getValue("root")
        if (rootRole.keyIds.size !in 2..5 || rootRole.threshold !in 2..rootRole.keyIds.size) return null
        val onlineRoleNames = listOf("targets", "snapshot", "timestamp")
        val onlineKeyIds = mutableSetOf<String>()
        onlineRoleNames.forEach { roleName ->
            val role = roles.getValue(roleName)
            if (role.threshold != 1 || role.keyIds.size != 1 || !onlineKeyIds.add(role.keyIds.single())) return null
        }
        if (rootRole.keyIds.any(onlineKeyIds::contains)) return null
        if (roles.values.flatMap { it.keyIds }.toSet() != publicKeys.keys) return null

        val signatures = envelope.optJSONArray("signatures") ?: return null
        if (signatures.length() !in rootRole.threshold..MAXIMUM_SIGNATURE_COUNT) return null
        val canonicalSigned = canonicalTufJson(signed)
        val verifiedRootKeys = mutableSetOf<String>()
        repeat(signatures.length()) { signatureIndex ->
            val signaturePayload = signatures.optJSONObject(signatureIndex) ?: return null
            if (!signaturePayload.hasExactly("keyid", "sig")) return null
            val keyId = signaturePayload.strictString("keyid") ?: return null
            val signature = signaturePayload.strictString("sig")?.decodeCanonicalHex(SIGNATURE_BYTES) ?: return null
            if (!rootRole.keyIds.contains(keyId) || !verifiedRootKeys.add(keyId)) return null
            val publicKey = publicKeys[keyId] ?: return null
            if (!signatureVerifier.verify(publicKey, canonicalSigned, signature)) return null
        }
        if (verifiedRootKeys.size < rootRole.threshold) return null
        VerifiedTufRoot(version, expiresAt)
    }.getOrNull()

    private fun parseRole(payload: JSONObject?, knownKeyIds: Set<String>): TufRole? {
        payload ?: return null
        if (!payload.hasExactly("keyids", "threshold")) return null
        val threshold = payload.strictPositiveLong("threshold")?.takeIf { it <= Int.MAX_VALUE }?.toInt() ?: return null
        val keyIdsPayload = payload.optJSONArray("keyids") ?: return null
        if (keyIdsPayload.length() !in 1..MAXIMUM_KEY_COUNT) return null
        val keyIds = mutableSetOf<String>()
        repeat(keyIdsPayload.length()) { keyIndex ->
            val keyId = keyIdsPayload.opt(keyIndex) as? String ?: return null
            if (keyId.decodeCanonicalHex(SHA256_BYTES) == null || !knownKeyIds.contains(keyId) || !keyIds.add(keyId)) return null
        }
        return TufRole(keyIds, threshold).takeIf { threshold <= keyIds.size }
    }
}

internal fun canonicalTufJson(value: Any): ByteArray = buildString { appendCanonicalJson(value) }.toByteArray(StandardCharsets.UTF_8)

private fun StringBuilder.appendCanonicalJson(value: Any?) {
    when (value) {
        null, JSONObject.NULL -> append("null")
        is JSONObject -> {
            append('{')
            value.keys().asSequence().toList().sorted().forEachIndexed { index, name ->
                if (index > 0) append(',')
                append(JSONObject.quote(name))
                append(':')
                appendCanonicalJson(value.get(name))
            }
            append('}')
        }
        is JSONArray -> {
            append('[')
            repeat(value.length()) { index ->
                if (index > 0) append(',')
                appendCanonicalJson(value.get(index))
            }
            append(']')
        }
        is String -> append(JSONObject.quote(value))
        is Boolean -> append(if (value) "true" else "false")
        is Byte, is Short, is Int, is Long -> append(value.toString())
        else -> throw IllegalArgumentException("unsupported TUF canonical JSON value")
    }
}

private fun JSONObject.hasExactly(vararg names: String): Boolean = keys().asSequence().toSet() == names.toSet()

private fun JSONObject.strictString(name: String): String? =
    (opt(name) as? String)?.takeIf { it.isNotEmpty() && it.none(Char::isISOControl) }

private fun JSONObject.strictPositiveLong(name: String): Long? {
    val value = opt(name)
    if (value !is Int && value !is Long) return null
    return value.toLong().takeIf { it > 0 }
}

private fun String.decodeCanonicalHex(expectedBytes: Int): ByteArray? {
    if (length != expectedBytes * 2 || any { character -> character !in '0'..'9' && character !in 'a'..'f' }) return null
    return runCatching {
        ByteArray(expectedBytes) { byteIndex -> substring(byteIndex * 2, byteIndex * 2 + 2).toInt(16).toByte() }
    }.getOrNull()
}

private fun ByteArray.sha256Hex(): String =
    MessageDigest.getInstance("SHA-256").digest(this).joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }

private val REQUIRED_ROLES = setOf("root", "snapshot", "targets", "timestamp")
private const val MAXIMUM_ROOT_BYTES = 1024 * 1024
private const val MINIMUM_KEY_COUNT = 5
private const val MAXIMUM_KEY_COUNT = 16
private const val MAXIMUM_SIGNATURE_COUNT = 16
private const val PUBLIC_KEY_BYTES = 32
private const val SIGNATURE_BYTES = 64
private const val SHA256_BYTES = 32
