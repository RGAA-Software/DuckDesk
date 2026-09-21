package yun.pixels.client.core.network

import java.security.KeyPair
import java.security.KeyPairGenerator
import java.security.MessageDigest
import java.security.Signature
import java.time.Instant
import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test

class AndroidTufRootVerifierTest {
    @Test
    fun independentlySignedThresholdRootBecomesTheImmutableTrustStartingPoint() {
        val fixture = RootFixture()
        val rootBytes = fixture.rootBytes()

        val configuration = AndroidTufTrustConfiguration.create(rootBytes, NOW, JcaEd25519Verifier())

        assertNotNull(configuration)
        assertEquals(1L, configuration?.initialRootVersion)
        assertEquals(NOW + 3_600, configuration?.initialRootExpiresAtEpochSeconds)
        assertEquals(true, configuration?.initialRootBytes?.contentEquals(rootBytes))
    }

    @Test
    fun oneSignatureCannotSatisfyTheRequiredRootThreshold() {
        val fixture = RootFixture()

        val configuration = AndroidTufTrustConfiguration.create(
            fixture.rootBytes(rootSignatureCount = 1),
            NOW,
            JcaEd25519Verifier(),
        )

        assertNull(configuration)
    }

    @Test
    fun expiredRootAndUnsignedSignedPayloadChangesFailClosed() {
        val fixture = RootFixture()
        assertNull(
            AndroidTufTrustConfiguration.create(
                fixture.rootBytes(expiresAtEpochSeconds = NOW - 1),
                NOW,
                JcaEd25519Verifier(),
            ),
        )
        val tamperedEnvelope = JSONObject(String(fixture.rootBytes(), Charsets.UTF_8))
        tamperedEnvelope.getJSONObject("signed").put("version", 2)
        assertNull(
            AndroidTufTrustConfiguration.create(
                tamperedEnvelope.toString().toByteArray(),
                NOW,
                JcaEd25519Verifier(),
            ),
        )
    }

    @Test
    fun rootRejectsKeyIdSubstitutionAndRoleKeyReuse() {
        val fixture = RootFixture()
        val wrongKeyId = JSONObject(String(fixture.rootBytes(), Charsets.UTF_8))
        val signedWithWrongKey = wrongKeyId.getJSONObject("signed")
        val keys = signedWithWrongKey.getJSONObject("keys")
        val originalKeyId = keys.keys().next()
        val keyPayload = keys.remove(originalKeyId)
        keys.put("f".repeat(64), keyPayload)
        assertNull(AndroidTufTrustConfiguration.create(wrongKeyId.toString().toByteArray(), NOW, JcaEd25519Verifier()))

        val reusedRoleKey = fixture.rootBytes { signed ->
            val roles = signed.getJSONObject("roles")
            val rootKeyId = roles.getJSONObject("root").getJSONArray("keyids").getString(0)
            roles.getJSONObject("targets").put("keyids", JSONArray().put(rootKeyId))
        }
        assertNull(AndroidTufTrustConfiguration.create(reusedRoleKey, NOW, JcaEd25519Verifier()))
    }

    private data class SigningKey(
        val pair: KeyPair,
        val keyPayload: JSONObject,
        val keyId: String,
    )

    private class RootFixture {
        private val rootKeys = List(2) { signingKey() }
        private val targetsKey = signingKey()
        private val snapshotKey = signingKey()
        private val timestampKey = signingKey()
        private val allKeys = rootKeys + targetsKey + snapshotKey + timestampKey

        fun rootBytes(
            expiresAtEpochSeconds: Long = NOW + 3_600,
            rootSignatureCount: Int = rootKeys.size,
            mutateSigned: (JSONObject) -> Unit = {},
        ): ByteArray {
            val keys = JSONObject()
            allKeys.forEach { signingKey -> keys.put(signingKey.keyId, signingKey.keyPayload) }
            val roles = JSONObject()
                .put("root", role(rootKeys.map(SigningKey::keyId), 2))
                .put("targets", role(listOf(targetsKey.keyId), 1))
                .put("snapshot", role(listOf(snapshotKey.keyId), 1))
                .put("timestamp", role(listOf(timestampKey.keyId), 1))
            val signed = JSONObject()
                .put("_type", "root")
                .put("spec_version", "1.0.0")
                .put("consistent_snapshot", true)
                .put("version", 1)
                .put("expires", Instant.ofEpochSecond(expiresAtEpochSeconds).toString())
                .put("keys", keys)
                .put("roles", roles)
            mutateSigned(signed)
            val canonicalSigned = canonicalTufJson(signed)
            val signatures = JSONArray()
            rootKeys.take(rootSignatureCount).forEach { signingKey ->
                signatures.put(
                    JSONObject()
                        .put("keyid", signingKey.keyId)
                        .put("sig", sign(signingKey.pair, canonicalSigned).toHex()),
                )
            }
            return JSONObject().put("signed", signed).put("signatures", signatures).toString().toByteArray()
        }

        private fun role(keyIds: List<String>, threshold: Int): JSONObject =
            JSONObject().put("keyids", JSONArray(keyIds)).put("threshold", threshold)
    }

    private companion object {
        const val NOW = 1_800_000_000L

        fun signingKey(): SigningKey {
            val pair = KeyPairGenerator.getInstance("Ed25519").generateKeyPair()
            val encodedPublicKey = pair.public.encoded
            val rawPublicKey = encodedPublicKey.copyOfRange(encodedPublicKey.size - 32, encodedPublicKey.size)
            val keyPayload = JSONObject()
                .put("keytype", "ed25519")
                .put("scheme", "ed25519")
                .put("keyval", JSONObject().put("public", rawPublicKey.toHex()))
            val keyId = MessageDigest.getInstance("SHA-256").digest(canonicalTufJson(keyPayload)).toHex()
            return SigningKey(pair, keyPayload, keyId)
        }

        fun sign(pair: KeyPair, message: ByteArray): ByteArray {
            val signer = Signature.getInstance("Ed25519")
            signer.initSign(pair.private)
            signer.update(message)
            return signer.sign()
        }

        fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
    }
}
