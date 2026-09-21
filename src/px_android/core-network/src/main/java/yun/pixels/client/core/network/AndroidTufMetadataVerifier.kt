package yun.pixels.client.core.network

import java.nio.charset.StandardCharsets
import java.security.MessageDigest
import java.time.Instant
import org.json.JSONObject
import yun.pixels.client.core.domain.update.AndroidUpdateRelease

internal data class AndroidTufMetadataVersions(
    val timestamp: Long,
    val snapshot: Long,
    val targets: Long,
)

internal class AndroidTufMetadataVerifier(
    private val signatureVerifier: DeploymentSignatureVerifier,
) {
    fun verify(
        trustedRoot: VerifiedTufRoot,
        releaseIdentity: AndroidReleaseIdentity,
        release: AndroidUpdateRelease,
        timestampBytes: ByteArray,
        snapshotBytes: ByteArray,
        targetsBytes: ByteArray,
        nowEpochSeconds: Long,
    ): AndroidTufMetadataVersions? {
        if (nowEpochSeconds < 0 || release.repositoryRootVersion != trustedRoot.version) return null
        val timestamp = parseEnvelope(timestampBytes, "timestamp", trustedRoot, nowEpochSeconds) ?: return null
        if (!timestamp.signed.hasExactly("_type", "spec_version", "version", "expires", "meta")) return null
        val snapshotDescription = parseOnlyMetafile(timestamp.signed, "snapshot.json") ?: return null
        if (!snapshotDescription.matches(snapshotBytes)) return null

        val snapshot = parseEnvelope(snapshotBytes, "snapshot", trustedRoot, nowEpochSeconds) ?: return null
        if (snapshot.version != snapshotDescription.version) return null
        if (!snapshot.signed.hasExactly("_type", "spec_version", "version", "expires", "meta")) return null
        val targetsDescription = parseOnlyMetafile(snapshot.signed, "targets.json") ?: return null
        if (!targetsDescription.matches(targetsBytes)) return null

        val targets = parseEnvelope(targetsBytes, "targets", trustedRoot, nowEpochSeconds) ?: return null
        if (targets.version != targetsDescription.version) return null
        if (!targets.signed.hasExactly("_type", "spec_version", "version", "expires", "targets")) return null
        if (!matchesApprovedTarget(targets.signed.optJSONObject("targets"), releaseIdentity, release)) return null
        return AndroidTufMetadataVersions(timestamp.version, snapshot.version, targets.version)
    }

    private fun parseEnvelope(
        metadataBytes: ByteArray,
        roleName: String,
        trustedRoot: VerifiedTufRoot,
        nowEpochSeconds: Long,
    ): SignedMetadata? = runCatching {
        if (metadataBytes.isEmpty() || metadataBytes.size > MAXIMUM_METADATA_BYTES) return null
        val envelope = JSONObject(String(metadataBytes, StandardCharsets.UTF_8))
        if (!envelope.hasExactly("signed", "signatures")) return null
        val signed = envelope.optJSONObject("signed") ?: return null
        if (signed.strictText("_type") != roleName || signed.strictText("spec_version") != "1.0.0") return null
        val version = signed.strictPositiveLong("version") ?: return null
        val expiresAt = signed.strictText("expires")
            ?.let { encodedTime -> Instant.parse(encodedTime).epochSecond }
            ?: return null
        if (expiresAt <= nowEpochSeconds) return null
        val role = trustedRoot.roles[roleName] ?: return null
        val canonicalSigned = canonicalTufJson(signed)
        val signatures = envelope.optJSONArray("signatures") ?: return null
        if (signatures.length() !in 1..MAXIMUM_SIGNATURE_COUNT) return null
        val observedKeyIds = mutableSetOf<String>()
        var verifiedSignatureCount = 0
        repeat(signatures.length()) { signatureIndex ->
            val signaturePayload = signatures.optJSONObject(signatureIndex) ?: return null
            if (!signaturePayload.hasExactly("keyid", "sig")) return null
            val keyId = signaturePayload.strictText("keyid") ?: return null
            val signature = signaturePayload.strictText("sig")?.decodeCanonicalHex(SIGNATURE_BYTES) ?: return null
            if (keyId.decodeCanonicalHex(SHA256_BYTES) == null || !observedKeyIds.add(keyId)) return null
            if (keyId in role.keyIds) {
                val publicKey = trustedRoot.publicKeys[keyId] ?: return null
                if (!signatureVerifier.verify(publicKey, canonicalSigned, signature)) return null
                verifiedSignatureCount += 1
            }
        }
        if (verifiedSignatureCount < role.threshold) return null
        SignedMetadata(signed, version)
    }.getOrNull()

    private fun parseOnlyMetafile(signed: JSONObject, expectedName: String): MetadataDescription? {
        val metadata = signed.optJSONObject("meta") ?: return null
        if (metadata.keys().asSequence().toSet() != setOf(expectedName)) return null
        val description = metadata.optJSONObject(expectedName) ?: return null
        if (!description.hasExactly("length", "hashes", "version")) return null
        val length = description.strictPositiveLong("length")?.takeIf { it <= MAXIMUM_METADATA_BYTES } ?: return null
        val hashes = description.optJSONObject("hashes") ?: return null
        if (!hashes.hasExactly("sha256")) return null
        val sha256 = hashes.strictText("sha256")?.takeIf(::isCanonicalSha256) ?: return null
        val version = description.strictPositiveLong("version") ?: return null
        return MetadataDescription(length, sha256, version)
    }

    private fun matchesApprovedTarget(
        targets: JSONObject?,
        releaseIdentity: AndroidReleaseIdentity,
        release: AndroidUpdateRelease,
    ): Boolean {
        targets ?: return false
        if (targets.length() !in 1..MAXIMUM_TARGET_COUNT) return false
        val artifact = release.artifact
        val target = targets.optJSONObject(artifact.targetName) ?: return false
        if (!target.hasExactly("length", "hashes", "custom")) return false
        if (target.strictPositiveLong("length") != artifact.sizeBytes) return false
        val hashes = target.optJSONObject("hashes") ?: return false
        if (!hashes.hasExactly("sha256") || hashes.strictText("sha256") != artifact.sha256) return false
        val custom = target.optJSONObject("custom") ?: return false
        if (!custom.hasExactly("pixels")) return false
        val pixels = custom.optJSONObject("pixels") ?: return false
        if (!pixels.hasExactly("schema_version", "target", "build_number", "version", "platform_signer_sha256")) {
            return false
        }
        if (
            pixels.strictPositiveLong("schema_version") != 1L ||
            pixels.strictPositiveLong("build_number") != artifact.buildNumber
        ) {
            return false
        }
        if (
            pixels.strictText("version") != artifact.version ||
            pixels.strictText("platform_signer_sha256") != artifact.platformSignerSha256
        ) {
            return false
        }
        val targetIdentity = pixels.optJSONObject("target") ?: return false
        if (
            !targetIdentity.hasExactly(
                "product",
                "distribution",
                "release_namespace",
                "oem_id",
                "channel",
                "os",
                "architecture",
            )
        ) {
            return false
        }
        return targetIdentity.strictText("product") == "android" &&
            targetIdentity.strictText("distribution") == releaseIdentity.distribution &&
            targetIdentity.strictText("release_namespace") == releaseIdentity.releaseNamespace &&
            targetIdentity.strictNullableText("oem_id") == releaseIdentity.oemId &&
            targetIdentity.strictText("channel") == "stable" &&
            targetIdentity.strictText("os") == "android" &&
            targetIdentity.strictText("architecture") == "aarch64"
    }
}

private data class SignedMetadata(
    val signed: JSONObject,
    val version: Long,
)

private data class MetadataDescription(
    val length: Long,
    val sha256: String,
    val version: Long,
) {
    fun matches(metadataBytes: ByteArray): Boolean =
        metadataBytes.size.toLong() == length &&
            MessageDigest.getInstance("SHA-256").digest(metadataBytes).toHex() == sha256
}

private fun JSONObject.hasExactly(vararg names: String): Boolean = keys().asSequence().toSet() == names.toSet()

private fun JSONObject.strictText(name: String): String? =
    (opt(name) as? String)?.takeIf { value -> value.isNotEmpty() && value.none(Char::isISOControl) }

private fun JSONObject.strictNullableText(name: String): String? {
    val value = opt(name)
    if (value === JSONObject.NULL) return null
    return (value as? String)
        ?.takeIf { candidate -> candidate.isNotEmpty() && candidate.none(Char::isISOControl) }
        ?: INVALID_NULLABLE_TEXT
}

private fun JSONObject.strictPositiveLong(name: String): Long? {
    val value = opt(name)
    if (value !is Int && value !is Long) return null
    return value.toLong().takeIf { candidate -> candidate > 0 }
}

private fun String.decodeCanonicalHex(expectedBytes: Int): ByteArray? {
    if (
        length != expectedBytes * 2 ||
        any { character -> character !in '0'..'9' && character !in 'a'..'f' }
    ) {
        return null
    }
    return runCatching {
        ByteArray(expectedBytes) { byteIndex -> substring(byteIndex * 2, byteIndex * 2 + 2).toInt(16).toByte() }
    }.getOrNull()
}

private fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }

private fun isCanonicalSha256(value: String): Boolean = value.decodeCanonicalHex(SHA256_BYTES) != null

private const val INVALID_NULLABLE_TEXT = "\u0000"
private const val MAXIMUM_METADATA_BYTES = 1024 * 1024L
private const val MAXIMUM_SIGNATURE_COUNT = 16
private const val MAXIMUM_TARGET_COUNT = 4096
private const val SIGNATURE_BYTES = 64
private const val SHA256_BYTES = 32
