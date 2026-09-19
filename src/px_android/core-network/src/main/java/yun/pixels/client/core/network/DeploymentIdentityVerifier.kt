package yun.pixels.client.core.network

import java.nio.charset.StandardCharsets
import java.security.KeyFactory
import java.security.MessageDigest
import java.security.Provider
import java.security.Signature
import java.security.spec.X509EncodedKeySpec
import java.util.Base64
import java.util.UUID
import org.conscrypt.Conscrypt
import org.json.JSONArray
import org.json.JSONObject

internal enum class DeploymentKind(val wireValue: String) {
    Official("official"),
    Private("private"),
}

internal data class DeploymentVerificationPolicy(
    val expectedKind: DeploymentKind,
    val expectedDeploymentId: UUID?,
    val minimumCertificateVersion: Long,
    val minimumDescriptorRevision: Long,
    val minimumTrustEpoch: Long,
    val clientBuild: Long,
    val protocolVersion: Int,
)

internal data class VerifiedDeploymentIdentity(
    val deploymentId: UUID,
    val deploymentKind: DeploymentKind,
    val deploymentPublicKey: ByteArray,
    val descriptorRevision: Long,
)

internal class DeploymentTrustStore private constructor(
    val trustEpoch: Long,
    private val trustedKeys: Map<String, ByteArray>,
) {
    fun publicKey(keyId: String): ByteArray? = trustedKeys[keyId]?.copyOf()

    companion object {
        fun parse(canonicalBytes: ByteArray): DeploymentTrustStore? = runCatching {
            val payload = JSONObject(String(canonicalBytes, StandardCharsets.UTF_8))
            if (!payload.hasExactly("schema_version", "trust_epoch", "trusted_keys")) return null
            if (payload.strictLong("schema_version") != 1L) return null
            val trustEpoch = payload.strictPositiveLong("trust_epoch") ?: return null
            val keys = payload.optJSONArray("trusted_keys") ?: return null
            if (keys.length() !in 1..16) return null
            val trustedKeys = linkedMapOf<String, ByteArray>()
            var previousKeyId: String? = null
            repeat(keys.length()) { index ->
                val key = keys.optJSONObject(index) ?: return null
                if (!key.hasExactly("key_id", "public_key_hex")) return null
                val keyId = key.strictString("key_id") ?: return null
                val publicKeyHex = key.strictString("public_key_hex") ?: return null
                val publicKey = publicKeyHex.decodeCanonicalHex(32) ?: return null
                if (keyId != publicKey.sha256Hex() || previousKeyId?.let { it >= keyId } == true) return null
                if (trustedKeys.put(keyId, publicKey) != null) return null
                previousKeyId = keyId
            }
            val canonical = buildString {
                append("{\"schema_version\":1,\"trust_epoch\":")
                append(trustEpoch)
                append(",\"trusted_keys\":[")
                trustedKeys.entries.forEachIndexed { index, (keyId, publicKey) ->
                    if (index > 0) append(',')
                    append("{\"key_id\":\"")
                    append(keyId)
                    append("\",\"public_key_hex\":\"")
                    append(publicKey.toHex())
                    append("\"}")
                }
                append("]}")
            }.toByteArray(StandardCharsets.UTF_8)
            if (!canonicalBytes.contentEquals(canonical)) return null
            DeploymentTrustStore(trustEpoch, trustedKeys)
        }.getOrNull()
    }
}

internal fun interface DeploymentSignatureVerifier {
    fun verify(publicKey: ByteArray, message: ByteArray, signature: ByteArray): Boolean
}

internal class JcaEd25519Verifier(private val provider: Provider? = null) : DeploymentSignatureVerifier {
    override fun verify(publicKey: ByteArray, message: ByteArray, signature: ByteArray): Boolean = runCatching {
        val keyFactory = provider?.let { KeyFactory.getInstance(ED25519, it) } ?: KeyFactory.getInstance(ED25519)
        val verifier = provider?.let { Signature.getInstance(ED25519, it) } ?: Signature.getInstance(ED25519)
        val encodedPublicKey = ED25519_X509_PREFIX + publicKey
        verifier.initVerify(keyFactory.generatePublic(X509EncodedKeySpec(encodedPublicKey)))
        verifier.update(message)
        verifier.verify(signature)
    }.getOrDefault(false)

    companion object {
        fun android(): JcaEd25519Verifier = JcaEd25519Verifier(Conscrypt.newProvider())
    }
}

internal class DeploymentIdentityVerifier(
    private val trustStore: DeploymentTrustStore,
    private val signatureVerifier: DeploymentSignatureVerifier,
) {
    fun verifyIdentity(identityJson: String, policy: DeploymentVerificationPolicy, nowEpochSeconds: Long): VerifiedDeploymentIdentity? =
        runCatching {
            if (!policy.isValid() || nowEpochSeconds < 0 || trustStore.trustEpoch < policy.minimumTrustEpoch) return null
            val identity = JSONObject(identityJson)
            if (!identity.hasExactly("certificate_wire", "descriptor_wire")) return null
            val certificateWire = identity.strictString("certificate_wire") ?: return null
            val descriptorWire = identity.strictString("descriptor_wire") ?: return null
            val certificatePayload = verifyWire(certificateWire, CERTIFICATE_PREFIX, CERTIFICATE_DOMAIN) { certificateBytes ->
                val certificate = JSONObject(String(certificateBytes, StandardCharsets.UTF_8))
                val issuerKeyId = certificate.strictString("issuer_key_id") ?: return@verifyWire null
                trustStore.publicKey(issuerKeyId)
            } ?: return null
            val certificate = parseCertificate(certificatePayload) ?: return null
            if (
                certificate.deploymentKind != policy.expectedKind ||
                policy.expectedDeploymentId?.let { it != certificate.deploymentId } == true ||
                certificate.certificateVersion < policy.minimumCertificateVersion ||
                certificate.notBefore > nowEpochSeconds ||
                certificate.expiresAt <= nowEpochSeconds
            ) return null
            val descriptorPayload = verifyWire(
                descriptorWire,
                DESCRIPTOR_PREFIX,
                DESCRIPTOR_DOMAIN,
            ) { certificate.deploymentPublicKey } ?: return null
            val descriptor = parseDescriptor(descriptorPayload) ?: return null
            if (
                descriptor.deploymentId != certificate.deploymentId ||
                descriptor.deploymentKind != certificate.deploymentKind ||
                descriptor.descriptorRevision < policy.minimumDescriptorRevision ||
                descriptor.trustEpoch < policy.minimumTrustEpoch ||
                descriptor.issuedAt > nowEpochSeconds ||
                descriptor.expiresAt <= nowEpochSeconds ||
                descriptor.minimumClientBuild > policy.clientBuild ||
                policy.protocolVersion !in descriptor.minimumProtocolVersion..descriptor.maximumProtocolVersion
            ) return null
            VerifiedDeploymentIdentity(
                certificate.deploymentId,
                certificate.deploymentKind,
                certificate.deploymentPublicKey,
                descriptor.descriptorRevision,
            )
        }.getOrNull()

    fun verifyChallenge(
        identity: VerifiedDeploymentIdentity,
        proofWire: String,
        expectedNonce: String,
        nowEpochSeconds: Long,
    ): Boolean = runCatching {
        val expectedNonceBytes = expectedNonce.decodeCanonicalBase64Url() ?: return false
        if (expectedNonceBytes.size != 32 || nowEpochSeconds < 0) return false
        val proofPayload = verifyWire(proofWire, CHALLENGE_PREFIX, CHALLENGE_DOMAIN) { identity.deploymentPublicKey }
            ?: return false
        val challenge = JSONObject(String(proofPayload, StandardCharsets.UTF_8))
        if (!challenge.hasExactly("schema_version", "deployment_id", "descriptor_revision", "nonce", "issued_at", "expires_at")) {
            return false
        }
        val deploymentId = challenge.strictUuid("deployment_id") ?: return false
        val descriptorRevision = challenge.strictPositiveLong("descriptor_revision") ?: return false
        val nonce = challenge.strictString("nonce") ?: return false
        val issuedAt = challenge.strictNonNegativeLong("issued_at") ?: return false
        val expiresAt = challenge.strictNonNegativeLong("expires_at") ?: return false
        deploymentId == identity.deploymentId &&
            descriptorRevision == identity.descriptorRevision &&
            nonce == expectedNonce &&
            expiresAt > issuedAt &&
            expiresAt - issuedAt <= 60 &&
            issuedAt <= nowEpochSeconds &&
            expiresAt > nowEpochSeconds
    }.getOrDefault(false)

    private fun verifyWire(
        wire: String,
        expectedPrefix: String,
        domain: ByteArray,
        publicKey: (ByteArray) -> ByteArray?,
    ): ByteArray? {
        if (wire.isEmpty() || wire.length > MAX_WIRE_CHARACTERS) return null
        val parts = wire.split('.')
        if (parts.size != 3 || parts[0] != expectedPrefix) return null
        val payload = parts[1].decodeCanonicalBase64Url() ?: return null
        val signature = parts[2].decodeCanonicalBase64Url() ?: return null
        if (signature.size != 64) return null
        val verificationKey = publicKey(payload) ?: return null
        if (verificationKey.size != 32 || verificationKey.all { it == 0.toByte() }) return null
        return payload.takeIf { signatureVerifier.verify(verificationKey, domain + payload, signature) }
    }
}

private data class DeploymentCertificatePayload(
    val deploymentId: UUID,
    val deploymentKind: DeploymentKind,
    val deploymentPublicKey: ByteArray,
    val certificateVersion: Long,
    val notBefore: Long,
    val expiresAt: Long,
)

private data class PlatformDescriptorPayload(
    val deploymentId: UUID,
    val deploymentKind: DeploymentKind,
    val descriptorRevision: Long,
    val trustEpoch: Long,
    val issuedAt: Long,
    val expiresAt: Long,
    val minimumClientBuild: Long,
    val minimumProtocolVersion: Int,
    val maximumProtocolVersion: Int,
)

private fun parseCertificate(payload: ByteArray): DeploymentCertificatePayload? = runCatching {
    val certificate = JSONObject(String(payload, StandardCharsets.UTF_8))
    if (!certificate.hasExactly(
            "schema_version",
            "deployment_id",
            "deployment_kind",
            "deployment_public_key_hex",
            "certificate_version",
            "not_before",
            "expires_at",
            "issuer_key_id",
        )
    ) return null
    if (certificate.strictLong("schema_version") != 1L) return null
    val deploymentId = certificate.strictUuid("deployment_id") ?: return null
    val deploymentKind = certificate.strictDeploymentKind("deployment_kind") ?: return null
    val deploymentPublicKey = certificate.strictString("deployment_public_key_hex")?.decodeCanonicalHex(32) ?: return null
    val certificateVersion = certificate.strictPositiveLong("certificate_version") ?: return null
    val notBefore = certificate.strictNonNegativeLong("not_before") ?: return null
    val expiresAt = certificate.strictNonNegativeLong("expires_at") ?: return null
    val issuerKeyId = certificate.strictString("issuer_key_id") ?: return null
    if (issuerKeyId.decodeCanonicalHex(32) == null || expiresAt <= notBefore || expiresAt > MAX_UNIX_SECONDS) return null
    DeploymentCertificatePayload(deploymentId, deploymentKind, deploymentPublicKey, certificateVersion, notBefore, expiresAt)
}.getOrNull()

private fun parseDescriptor(payload: ByteArray): PlatformDescriptorPayload? = runCatching {
    val descriptor = JSONObject(String(payload, StandardCharsets.UTF_8))
    if (!descriptor.hasExactly(
            "schema_version",
            "deployment_id",
            "deployment_kind",
            "descriptor_revision",
            "trust_epoch",
            "issued_at",
            "expires_at",
            "minimum_client_build",
            "api_versions",
            "minimum_protocol_version",
            "maximum_protocol_version",
            "authentication_methods",
            "registration_policy",
            "console_api_path",
            "node_control_path",
        )
    ) return null
    if (descriptor.strictLong("schema_version") != 1L) return null
    val deploymentId = descriptor.strictUuid("deployment_id") ?: return null
    val deploymentKind = descriptor.strictDeploymentKind("deployment_kind") ?: return null
    val descriptorRevision = descriptor.strictPositiveLong("descriptor_revision") ?: return null
    val trustEpoch = descriptor.strictPositiveLong("trust_epoch") ?: return null
    val issuedAt = descriptor.strictNonNegativeLong("issued_at") ?: return null
    val expiresAt = descriptor.strictNonNegativeLong("expires_at") ?: return null
    val minimumClientBuild = descriptor.strictPositiveLong("minimum_client_build") ?: return null
    val apiVersions = descriptor.strictSortedStringArray("api_versions", 1..16) ?: return null
    val minimumProtocolVersion = descriptor.strictPositiveInt("minimum_protocol_version") ?: return null
    val maximumProtocolVersion = descriptor.strictPositiveInt("maximum_protocol_version") ?: return null
    val authenticationMethods = descriptor.strictSortedStringArray("authentication_methods", 1..8) ?: return null
    val registrationPolicy = descriptor.strictString("registration_policy") ?: return null
    if (
        expiresAt <= issuedAt || expiresAt - issuedAt > 86_400 ||
        apiVersions.any { !it.isWireToken() } ||
        maximumProtocolVersion < minimumProtocolVersion ||
        authenticationMethods.any { it !in setOf("guest", "password") } ||
        registrationPolicy !in setOf("closed", "open") ||
        descriptor.strictString("console_api_path") != "/api/console" ||
        descriptor.strictString("node_control_path") != "/api/console/node-control"
    ) return null
    PlatformDescriptorPayload(
        deploymentId,
        deploymentKind,
        descriptorRevision,
        trustEpoch,
        issuedAt,
        expiresAt,
        minimumClientBuild,
        minimumProtocolVersion,
        maximumProtocolVersion,
    )
}.getOrNull()

private fun DeploymentVerificationPolicy.isValid(): Boolean =
    minimumCertificateVersion > 0 &&
        minimumDescriptorRevision > 0 &&
        minimumTrustEpoch > 0 &&
        clientBuild > 0 &&
        protocolVersion in 1..UShort.MAX_VALUE.toInt()

private fun JSONObject.hasExactly(vararg names: String): Boolean = keys().asSequence().toSet() == names.toSet()

private fun JSONObject.strictString(name: String): String? = opt(name).takeIf { it is String } as? String

private fun JSONObject.strictLong(name: String): Long? {
    val number = opt(name) as? Number ?: return null
    val longValue = number.toLong()
    return longValue.takeIf { number.toDouble().isFinite() && number.toDouble() == longValue.toDouble() }
}

private fun JSONObject.strictNonNegativeLong(name: String): Long? = strictLong(name)?.takeIf { it >= 0 }

private fun JSONObject.strictPositiveLong(name: String): Long? = strictLong(name)?.takeIf { it > 0 }

private fun JSONObject.strictPositiveInt(name: String): Int? = strictPositiveLong(name)?.takeIf { it <= UShort.MAX_VALUE.toLong() }?.toInt()

private fun JSONObject.strictUuid(name: String): UUID? {
    val value = strictString(name) ?: return null
    val identifier = runCatching { UUID.fromString(value) }.getOrNull() ?: return null
    return identifier.takeIf { identifier != ZERO_UUID && identifier.toString() == value }
}

private fun JSONObject.strictDeploymentKind(name: String): DeploymentKind? = when (strictString(name)) {
    DeploymentKind.Official.wireValue -> DeploymentKind.Official
    DeploymentKind.Private.wireValue -> DeploymentKind.Private
    else -> null
}

private fun JSONObject.strictSortedStringArray(name: String, allowedSize: IntRange): List<String>? {
    val values = opt(name) as? JSONArray ?: return null
    if (values.length() !in allowedSize) return null
    val result = buildList {
        repeat(values.length()) { index -> add(values.opt(index).takeIf { it is String } as? String ?: return null) }
    }
    return result.takeIf { entries -> entries.zipWithNext().all { (left, right) -> left < right } }
}

private fun String.decodeCanonicalHex(expectedBytes: Int): ByteArray? {
    if (length != expectedBytes * 2 || any { it !in '0'..'9' && it !in 'a'..'f' }) return null
    return runCatching { chunked(2).map { it.toInt(16).toByte() }.toByteArray() }.getOrNull()
}

private fun String.decodeCanonicalBase64Url(): ByteArray? = runCatching {
    val decoded = Base64.getUrlDecoder().decode(this)
    decoded.takeIf { Base64.getUrlEncoder().withoutPadding().encodeToString(decoded) == this }
}.getOrNull()

private fun ByteArray.sha256Hex(): String =
    MessageDigest.getInstance("SHA-256").digest(this).joinToString("") { "%02x".format(it.toInt() and 0xff) }

private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it.toInt() and 0xff) }

private fun String.isWireToken(): Boolean =
    isNotEmpty() && length <= 32 && all { it in 'a'..'z' || it in '0'..'9' || it == '.' || it == '-' || it == '_' }

private const val ED25519 = "Ed25519"
private val ED25519_X509_PREFIX = byteArrayOf(0x30, 0x2a, 0x30, 0x05, 0x06, 0x03, 0x2b, 0x65, 0x70, 0x03, 0x21, 0x00)
private val ZERO_UUID = UUID(0, 0)
private const val MAX_WIRE_CHARACTERS = 16 * 1024
private const val MAX_UNIX_SECONDS = 253_402_300_799L
private const val CERTIFICATE_PREFIX = "PXDC1"
private val CERTIFICATE_DOMAIN = "Pixels-Deployment-Certificate-v1\u0000".toByteArray(StandardCharsets.UTF_8)
private const val DESCRIPTOR_PREFIX = "PXDD1"
private val DESCRIPTOR_DOMAIN = "Pixels-Platform-Descriptor-v1\u0000".toByteArray(StandardCharsets.UTF_8)
private const val CHALLENGE_PREFIX = "PXDP1"
private val CHALLENGE_DOMAIN = "Pixels-Deployment-Challenge-v1\u0000".toByteArray(StandardCharsets.UTF_8)
