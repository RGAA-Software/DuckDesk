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
            "PXDC1",
            "Pixels-Deployment-Certificate-v1\u0000",
            JSONObject()
                .put("schema_version", 1)
                .put("deployment_id", deploymentId.toString())
                .put("deployment_kind", "private")
                .put("deployment_public_key_hex", deploymentPublicKey.toHex())
                .put("certificate_version", 2)
                .put("not_before", NOW - 60)
                .put("expires_at", NOW + 86_400)
                .put("issuer_key_id", vendorKeyId)
                .toString(),
            vendorKey,
        )
        val descriptorWire = wire(
            "PXDD1",
            "Pixels-Platform-Descriptor-v1\u0000",
            JSONObject()
                .put("schema_version", 1)
                .put("deployment_id", deploymentId.toString())
                .put("deployment_kind", "private")
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
        val challengeWire = wire(
            "PXDP1",
            "Pixels-Deployment-Challenge-v1\u0000",
            JSONObject()
                .put("schema_version", 1)
                .put("deployment_id", deploymentId.toString())
                .put("descriptor_revision", 4)
                .put("nonce", nonce)
                .put("issued_at", NOW)
                .put("expires_at", NOW + 30)
                .toString(),
            deploymentKey,
        )
        val policy = DeploymentVerificationPolicy(
            expectedKind = DeploymentKind.Private,
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
