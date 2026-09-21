package yun.pixels.client.core.network

import java.nio.charset.StandardCharsets
import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.Signature
import java.util.Base64
import java.util.UUID
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.test.runTest
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.DeploymentIdentityWatermark
import yun.pixels.client.core.domain.account.DeploymentIdentityWatermarkState
import yun.pixels.client.core.domain.account.DeploymentIdentityWatermarkStore

class DeploymentIdentityVerifierTest {
    @Test
    fun matchingPrivateIdentityAndNonceProofAreAccepted() {
        val fixture = Fixture()
        val verified = fixture.verifier.verifyIdentity(fixture.identityJson, fixture.policy, NOW)
        assertNotNull(verified)
        assertTrue(fixture.verifier.verifyChallenge(verified!!, fixture.challengeWire, fixture.nonce, NOW + 1))
    }

    @Test
    fun wrongDistributionTamperingAndReplayAreRejected() {
        val fixture = Fixture()
        assertNull(
            fixture.verifier.verifyIdentity(
                fixture.identityJson,
                fixture.policy.copy(expectedKind = DeploymentKind.Official),
                NOW,
            ),
        )
        val tamperedWire = fixture.descriptorWire.dropLast(1) + if (fixture.descriptorWire.last() == 'A') "B" else "A"
        val tamperedIdentity = JSONObject(fixture.identityJson)
            .put("descriptor_wire", tamperedWire)
            .toString()
        assertNull(fixture.verifier.verifyIdentity(tamperedIdentity, fixture.policy, NOW))
        val verified = fixture.verifier.verifyIdentity(fixture.identityJson, fixture.policy, NOW)!!
        assertFalse(fixture.verifier.verifyChallenge(verified, fixture.challengeWire, fixture.otherNonce, NOW + 1))
        assertFalse(fixture.verifier.verifyChallenge(verified, fixture.challengeWire, fixture.nonce, NOW + 31))
    }

    @Test
    fun trustStoreRejectsUnknownUnsortedAndDuplicateKeys() {
        val fixture = Fixture()
        assertNull(DeploymentTrustStore.parse(fixture.trustStoreBytes + byteArrayOf('\n'.code.toByte())))
        val unknown = JSONObject(String(fixture.trustStoreBytes, StandardCharsets.UTF_8)).put("unexpected", true).toString()
        assertNull(DeploymentTrustStore.parse(unknown.toByteArray(StandardCharsets.UTF_8)))
        val duplicate = JSONObject(String(fixture.trustStoreBytes, StandardCharsets.UTF_8))
        duplicate.getJSONArray("trusted_keys").put(duplicate.getJSONArray("trusted_keys").getJSONObject(0))
        assertNull(DeploymentTrustStore.parse(duplicate.toString().toByteArray(StandardCharsets.UTF_8)))
    }

    @Test
    fun credentialsAreSentOnlyAfterIdentityAndNonceProof() = runTest {
        val fixture = Fixture()
        val requests = mutableListOf<Pair<String, String?>>()
        val executor = ConsoleRequestExecutor { _, path, _, _, _, body ->
            requests += path to body?.toString()
            when (path) {
                "/.well-known/pixels" -> HttpResponse(200, fixture.identityJson)
                "/.well-known/pixels/challenge" -> {
                    val nonce = body!!.getString("nonce")
                    HttpResponse(200, JSONObject().put("proof_wire", fixture.challengeWire(nonce)).toString())
                }
                "/api/console/sessions" -> HttpResponse(
                    200,
                    "{\"token\":\"token\",\"expires_at\":\"2030-01-01T00:00:00Z\",\"profile\":{\"id\":\"user-1\",\"username\":\"alice\",\"avatar_url\":null}}",
                )
                else -> HttpResponse(404, "{}")
            }
        }
        val configuration = DeploymentIdentityConfiguration.createForTesting(fixture.trustStoreBytes, fixture.policy)!!
        val watermarkStore = MemoryWatermarkStore()
        val client = ConsoleApiClient(Dispatchers.Unconfined, executor, configuration, watermarkStore) { NOW }

        val result = client.login("https://console.example", "alice", "secret-password")

        assertTrue(result is AccountResult.Success)
        assertTrue(requests.map { it.first } == listOf("/.well-known/pixels", "/.well-known/pixels/challenge", "/api/console/sessions"))
        assertFalse(requests[0].second.orEmpty().contains("secret-password"))
        assertFalse(requests[1].second.orEmpty().contains("secret-password"))
        assertTrue(requests[2].second.orEmpty().contains("secret-password"))
        assertTrue(watermarkStore.state is DeploymentIdentityWatermarkState.Present)
    }

    @Test
    fun persistedWatermarkRejectsRollbackBeforeCredentials() = runTest {
        val fixture = Fixture()
        val requests = mutableListOf<String>()
        val executor = ConsoleRequestExecutor { _, path, _, _, _, _ ->
            requests += path
            when (path) {
                "/.well-known/pixels" -> HttpResponse(200, fixture.identityJson)
                else -> HttpResponse(500, "{}")
            }
        }
        val watermarkStore = MemoryWatermarkStore(
            DeploymentIdentityWatermarkState.Present(
                DeploymentIdentityWatermark(
                    "9c08feb1-af71-4fab-a6b8-bbd99b3552ba",
                    "private",
                    "customer",
                    "pixels.customer",
                    null,
                    2,
                    5,
                    3,
                ),
            ),
        )
        val configuration = DeploymentIdentityConfiguration.createForTesting(
            fixture.trustStoreBytes,
            fixture.policy.copy(expectedDeploymentId = null),
        )!!
        val client = ConsoleApiClient(Dispatchers.Unconfined, executor, configuration, watermarkStore) { NOW }

        val result = client.login("https://console.example", "alice", "secret-password")

        assertTrue(result is AccountResult.Failure)
        assertTrue(requests == listOf("/.well-known/pixels"))
    }

    private class Fixture {
        private val vendorKey = keyPair()
        private val deploymentKey = keyPair()
        private val deploymentId = UUID.fromString("9c08feb1-af71-4fab-a6b8-bbd99b3552ba")
        val nonce = Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(32) { 7 })
        val otherNonce = Base64.getUrlEncoder().withoutPadding().encodeToString(ByteArray(32) { 8 })
        private val vendorPublicKey = rawPublicKey(vendorKey)
        private val deploymentPublicKey = rawPublicKey(deploymentKey)
        private val vendorKeyId = sha256Hex(vendorPublicKey)
        val trustStoreBytes =
            "{\"schema_version\":1,\"trust_epoch\":3,\"trusted_keys\":[{\"key_id\":\"$vendorKeyId\",\"public_key_hex\":\"${vendorPublicKey.toHex()}\"}]}"
                .toByteArray(StandardCharsets.UTF_8)
        private val certificateWire = wire(
            "PXDC2",
            "Pixels-Deployment-Certificate-v2\u0000",
            JSONObject()
                .put("schema_version", 2)
                .put("deployment_id", deploymentId.toString())
                .put("deployment_kind", "private")
                .put("distribution", "customer")
                .put("release_namespace", "pixels.customer")
                .put("oem_id", JSONObject.NULL)
                .put("deployment_public_key_hex", deploymentPublicKey.toHex())
                .put("certificate_version", 2)
                .put("not_before", NOW - 60)
                .put("expires_at", NOW + 86_400)
                .put("issuer_key_id", vendorKeyId)
                .toString(),
            vendorKey,
        )
        val descriptorWire = wire(
            "PXDD2",
            "Pixels-Platform-Descriptor-v2\u0000",
            JSONObject()
                .put("schema_version", 2)
                .put("deployment_id", deploymentId.toString())
                .put("deployment_kind", "private")
                .put("distribution", "customer")
                .put("release_namespace", "pixels.customer")
                .put("oem_id", JSONObject.NULL)
                .put("descriptor_revision", 4)
                .put("trust_epoch", 3)
                .put("issued_at", NOW)
                .put("expires_at", NOW + 300)
                .put("minimum_client_build", 20)
                .put("api_versions", JSONArray().put("console.v1").put("node.v1"))
                .put("minimum_protocol_version", 1)
                .put("maximum_protocol_version", 1)
                .put("authentication_methods", JSONArray().put("guest").put("password"))
                .put("registration_policy", "closed")
                .put("console_api_path", "/api/console")
                .put("node_control_path", "/api/console/node-control")
                .toString(),
            deploymentKey,
        )
        val identityJson = JSONObject()
            .put("certificate_wire", certificateWire)
            .put("descriptor_wire", descriptorWire)
            .toString()
        val challengeWire = challengeWire(nonce)
        fun challengeWire(nonceValue: String): String = wire(
            "PXDP1",
            "Pixels-Deployment-Challenge-v1\u0000",
            JSONObject()
                .put("schema_version", 1)
                .put("deployment_id", deploymentId.toString())
                .put("descriptor_revision", 4)
                .put("nonce", nonceValue)
                .put("issued_at", NOW)
                .put("expires_at", NOW + 30)
                .toString(),
            deploymentKey,
        )
        val policy = DeploymentVerificationPolicy(
            expectedKind = DeploymentKind.Private,
            expectedDistribution = DeploymentDistribution.Customer,
            expectedReleaseNamespace = "pixels.customer",
            expectedOemId = null,
            expectedDeploymentId = deploymentId,
            minimumCertificateVersion = 2,
            minimumDescriptorRevision = 4,
            minimumTrustEpoch = 3,
            clientBuild = 20,
            protocolVersion = 1,
        )
        val verifier = DeploymentIdentityVerifier(DeploymentTrustStore.parse(trustStoreBytes)!!, JcaEd25519Verifier())
    }

    companion object {
        private const val NOW = 1_700_000_000L

        private fun keyPair(): KeyPair = KeyPairGenerator.getInstance("Ed25519").generateKeyPair()

        private fun rawPublicKey(keyPair: KeyPair): ByteArray = keyPair.public.encoded.takeLast(32).toByteArray()

        private fun wire(prefix: String, domain: String, payloadJson: String, keyPair: KeyPair): String {
            val payload = payloadJson.toByteArray(StandardCharsets.UTF_8)
            val signer = Signature.getInstance("Ed25519")
            signer.initSign(keyPair.private)
            signer.update(domain.toByteArray(StandardCharsets.UTF_8) + payload)
            val encoder = Base64.getUrlEncoder().withoutPadding()
            return "$prefix.${encoder.encodeToString(payload)}.${encoder.encodeToString(signer.sign())}"
        }

        private fun sha256Hex(bytes: ByteArray): String =
            MessageDigest.getInstance("SHA-256").digest(bytes).toHex()

        private fun ByteArray.toHex(): String = joinToString("") { "%02x".format(it.toInt() and 0xff) }
    }
}

private class MemoryWatermarkStore(
    var state: DeploymentIdentityWatermarkState = DeploymentIdentityWatermarkState.Empty,
) : DeploymentIdentityWatermarkStore {
    override fun load(): DeploymentIdentityWatermarkState = state

    override fun save(watermark: DeploymentIdentityWatermark): Boolean {
        state = DeploymentIdentityWatermarkState.Present(watermark)
        return true
    }
}
