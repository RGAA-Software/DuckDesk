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
import yun.pixels.client.core.domain.update.AndroidTufTrustedRoot
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootState
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootStore
import yun.pixels.client.core.domain.update.AndroidUpdateArtifact
import yun.pixels.client.core.domain.update.AndroidUpdateRelease

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

    @Test
    fun nextRootRequiresTheOldAndNewRootThresholds() {
        val fixture = RootFixture()
        val verifier = AndroidTufRootVerifier(JcaEd25519Verifier())
        val initialRoot = requireNotNull(verifier.verifyInitialRoot(fixture.rootBytes(), NOW))

        val rotatedRoot = verifier.verifyNextRoot(initialRoot, fixture.rotatedRootBytes(), NOW)

        assertNotNull(rotatedRoot)
        assertEquals(2L, rotatedRoot?.version)
    }

    @Test
    fun nextRootRejectsARevisionGap() {
        val fixture = RootFixture()
        val verifier = AndroidTufRootVerifier(JcaEd25519Verifier())
        val initialRoot = requireNotNull(verifier.verifyInitialRoot(fixture.rootBytes(), NOW))

        assertNull(verifier.verifyNextRoot(initialRoot, fixture.rotatedRootBytes(version = 3), NOW))
    }

    @Test
    fun nextRootRejectsAOneSidedAuthorization() {
        val fixture = RootFixture()
        val verifier = AndroidTufRootVerifier(JcaEd25519Verifier())
        val initialRoot = requireNotNull(verifier.verifyInitialRoot(fixture.rootBytes(), NOW))

        assertNull(
            verifier.verifyNextRoot(
                initialRoot,
                fixture.rotatedRootBytes(includeCurrentRootSignatures = false),
                NOW,
            ),
        )
        assertNull(
            verifier.verifyNextRoot(
                initialRoot,
                fixture.rotatedRootBytes(includeCandidateRootSignatures = false),
                NOW,
            ),
        )
    }

    @Test
    fun nextRootRejectsSignedPayloadTampering() {
        val fixture = RootFixture()
        val verifier = AndroidTufRootVerifier(JcaEd25519Verifier())
        val initialRoot = requireNotNull(verifier.verifyInitialRoot(fixture.rootBytes(), NOW))
        val tamperedEnvelope = JSONObject(String(fixture.rotatedRootBytes(), Charsets.UTF_8))
        tamperedEnvelope.getJSONObject("signed").put("expires", Instant.ofEpochSecond(NOW + 7_200).toString())

        assertNull(verifier.verifyNextRoot(initialRoot, tamperedEnvelope.toString().toByteArray(), NOW))
    }

    @Test
    fun trustedRootManagerPersistsOnlyAValidatedNextRoot() {
        val fixture = RootFixture()
        val initialRootBytes = fixture.rootBytes()
        val trustConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(initialRootBytes, NOW, JcaEd25519Verifier()),
        )
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val trustedRootStore = MemoryTrustedRootStore()
        val manager = requireNotNull(
            AndroidTufTrustedRootManager.create(
                trustConfiguration,
                releaseIdentity,
                trustedRootStore,
                NOW,
                JcaEd25519Verifier(),
            ),
        )

        assertEquals(1L, manager.currentVersion)
        assertEquals(1L, (trustedRootStore.state as AndroidTufTrustedRootState.Present).trustedRoot.version)
        assertEquals(true, manager.acceptNextRoot(fixture.rotatedRootBytes(), NOW))
        assertEquals(2L, manager.currentVersion)
        assertEquals(2L, (trustedRootStore.state as AndroidTufTrustedRootState.Present).trustedRoot.version)
    }

    @Test
    fun trustedRootManagerDoesNotAdvanceWhenAtomicPersistenceFails() {
        val fixture = RootFixture()
        val trustConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(fixture.rootBytes(), NOW, JcaEd25519Verifier()),
        )
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("customer", "pixels.customer", null))
        val trustedRootStore = MemoryTrustedRootStore()
        val manager = requireNotNull(
            AndroidTufTrustedRootManager.create(
                trustConfiguration,
                releaseIdentity,
                trustedRootStore,
                NOW,
                JcaEd25519Verifier(),
            ),
        )
        trustedRootStore.allowSave = false

        assertEquals(false, manager.acceptNextRoot(fixture.rotatedRootBytes(), NOW))
        assertEquals(1L, manager.currentVersion)
    }

    @Test
    fun trustedRootManagerRestoresThePersistedRootAndRejectsAnotherReleaseDomain() {
        val fixture = RootFixture()
        val trustConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(fixture.rootBytes(), NOW, JcaEd25519Verifier()),
        )
        val officialIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val trustedRootStore = MemoryTrustedRootStore()
        val firstManager = requireNotNull(
            AndroidTufTrustedRootManager.create(
                trustConfiguration,
                officialIdentity,
                trustedRootStore,
                NOW,
                JcaEd25519Verifier(),
            ),
        )
        assertEquals(true, firstManager.acceptNextRoot(fixture.rotatedRootBytes(), NOW))

        val restoredManager = AndroidTufTrustedRootManager.create(
            trustConfiguration,
            officialIdentity,
            trustedRootStore,
            NOW,
            JcaEd25519Verifier(),
        )
        val customerIdentity = requireNotNull(AndroidReleaseIdentity.create("customer", "pixels.customer", null))
        val mismatchedManager = AndroidTufTrustedRootManager.create(
            trustConfiguration,
            customerIdentity,
            trustedRootStore,
            NOW,
            JcaEd25519Verifier(),
        )

        assertEquals(2L, restoredManager?.currentVersion)
        assertNull(mismatchedManager)
    }

    @Test
    fun newerEmbeddedRootMustAdvanceFromThePersistedRoot() {
        val fixture = RootFixture()
        val initialRootBytes = fixture.rootBytes()
        val rotatedRootBytes = fixture.rotatedRootBytes()
        val initialConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(initialRootBytes, NOW, JcaEd25519Verifier()),
        )
        val rotatedConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(rotatedRootBytes, NOW, JcaEd25519Verifier()),
        )
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val trustedRootStore = MemoryTrustedRootStore()
        assertNotNull(
            AndroidTufTrustedRootManager.create(
                initialConfiguration,
                releaseIdentity,
                trustedRootStore,
                NOW,
                JcaEd25519Verifier(),
            ),
        )

        val upgradedManager = AndroidTufTrustedRootManager.create(
            rotatedConfiguration,
            releaseIdentity,
            trustedRootStore,
            NOW,
            JcaEd25519Verifier(),
        )

        assertEquals(2L, upgradedManager?.currentVersion)
        assertEquals(2L, (trustedRootStore.state as AndroidTufTrustedRootState.Present).trustedRoot.version)
    }

    @Test
    fun invalidPersistentRootStateNeverFallsBackToTheEmbeddedRoot() {
        val fixture = RootFixture()
        val trustConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(fixture.rootBytes(), NOW, JcaEd25519Verifier()),
        )
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val trustedRootStore = MemoryTrustedRootStore(AndroidTufTrustedRootState.Invalid)

        val manager = AndroidTufTrustedRootManager.create(
            trustConfiguration,
            releaseIdentity,
            trustedRootStore,
            NOW,
            JcaEd25519Verifier(),
        )

        assertNull(manager)
    }

    @Test
    fun timestampSnapshotAndTargetsChainMatchesTheApprovedAndroidRelease() {
        val fixture = RootFixture()
        val trustedRoot = requireNotNull(AndroidTufRootVerifier(JcaEd25519Verifier()).verifyInitialRoot(fixture.rootBytes(), NOW))
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val release = androidRelease()
        val metadata = fixture.metadata(release, releaseIdentity)

        val versions = AndroidTufMetadataVerifier(JcaEd25519Verifier()).verify(
            trustedRoot,
            releaseIdentity,
            release,
            metadata.timestampBytes,
            metadata.snapshotBytes,
            metadata.targetsBytes,
            NOW,
        )

        assertEquals(AndroidTufMetadataVersions(1, 1, 1), versions)
    }

    @Test
    fun metadataChainRejectsTamperingAndConsoleCatalogDisagreement() {
        val fixture = RootFixture()
        val trustedRoot = requireNotNull(AndroidTufRootVerifier(JcaEd25519Verifier()).verifyInitialRoot(fixture.rootBytes(), NOW))
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val release = androidRelease()
        val metadata = fixture.metadata(release, releaseIdentity)
        val verifier = AndroidTufMetadataVerifier(JcaEd25519Verifier())
        val tamperedTargets = metadata.targetsBytes.copyOf().also { bytes ->
            bytes[bytes.lastIndex] = (bytes.last().toInt() xor 1).toByte()
        }
        val wrongCatalogRelease = release.copy(artifact = release.artifact.copy(platformSignerSha256 = "c".repeat(64)))

        assertNull(
            verifier.verify(
                trustedRoot,
                releaseIdentity,
                release,
                metadata.timestampBytes,
                metadata.snapshotBytes,
                tamperedTargets,
                NOW,
            ),
        )
        assertNull(
            verifier.verify(
                trustedRoot,
                releaseIdentity,
                wrongCatalogRelease,
                metadata.timestampBytes,
                metadata.snapshotBytes,
                metadata.targetsBytes,
                NOW,
            ),
        )
    }

    @Test
    fun metadataWatermarkRejectsRollbackAndSameVersionEquivocation() {
        val fixture = RootFixture()
        val trustConfiguration = requireNotNull(
            AndroidTufTrustConfiguration.create(fixture.rootBytes(), NOW, JcaEd25519Verifier()),
        )
        val releaseIdentity = requireNotNull(AndroidReleaseIdentity.create("official", "pixels.official", null))
        val trustedRootStore = MemoryTrustedRootStore()
        val manager = requireNotNull(
            AndroidTufTrustedRootManager.create(
                trustConfiguration,
                releaseIdentity,
                trustedRootStore,
                NOW,
                JcaEd25519Verifier(),
            ),
        )
        val release = androidRelease()
        val currentMetadata = fixture.metadata(release, releaseIdentity, 2, 2, 2)
        assertEquals(
            true,
            manager.verifyAndCommitMetadata(
                release,
                currentMetadata.timestampBytes,
                currentMetadata.snapshotBytes,
                currentMetadata.targetsBytes,
                NOW,
            ),
        )
        val restoredManager = requireNotNull(
            AndroidTufTrustedRootManager.create(
                trustConfiguration,
                releaseIdentity,
                trustedRootStore,
                NOW,
                JcaEd25519Verifier(),
            ),
        )

        val rollbackMetadata = fixture.metadata(release, releaseIdentity, 1, 1, 1)
        val equivocatedMetadata = fixture.metadata(release, releaseIdentity, 2, 2, 2, includeUnrelatedTarget = true)
        assertEquals(
            false,
            restoredManager.verifyAndCommitMetadata(
                release,
                rollbackMetadata.timestampBytes,
                rollbackMetadata.snapshotBytes,
                rollbackMetadata.targetsBytes,
                NOW,
            ),
        )
        assertEquals(
            false,
            restoredManager.verifyAndCommitMetadata(
                release,
                equivocatedMetadata.timestampBytes,
                equivocatedMetadata.snapshotBytes,
                equivocatedMetadata.targetsBytes,
                NOW,
            ),
        )
    }

    private data class SigningKey(
        val pair: KeyPair,
        val keyPayload: JSONObject,
        val keyId: String,
    )

    private data class MetadataBundle(
        val timestampBytes: ByteArray,
        val snapshotBytes: ByteArray,
        val targetsBytes: ByteArray,
    )

    private class MemoryTrustedRootStore(
        var state: AndroidTufTrustedRootState = AndroidTufTrustedRootState.Empty,
        var allowSave: Boolean = true,
    ) : AndroidTufTrustedRootStore {
        override fun load(): AndroidTufTrustedRootState = state

        override fun save(trustedRoot: AndroidTufTrustedRoot): Boolean {
            if (!allowSave) return false
            state = AndroidTufTrustedRootState.Present(
                AndroidTufTrustedRoot(
                    trustedRoot.distribution,
                    trustedRoot.releaseNamespace,
                    trustedRoot.oemId,
                    trustedRoot.version,
                    trustedRoot.copyRootBytes(),
                    trustedRoot.metadataWatermark,
                ),
            )
            return true
        }
    }

    private class RootFixture {
        private val rootKeys = List(2) { signingKey() }
        private val targetsKey = signingKey()
        private val snapshotKey = signingKey()
        private val timestampKey = signingKey()
        private val allKeys = rootKeys + targetsKey + snapshotKey + timestampKey
        private val rotatedRootKeys = List(2) { signingKey() }
        private val rotatedTargetsKey = signingKey()
        private val rotatedSnapshotKey = signingKey()
        private val rotatedTimestampKey = signingKey()
        private val allRotatedKeys = rotatedRootKeys + rotatedTargetsKey + rotatedSnapshotKey + rotatedTimestampKey

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

        fun rotatedRootBytes(
            version: Long = 2,
            includeCurrentRootSignatures: Boolean = true,
            includeCandidateRootSignatures: Boolean = true,
        ): ByteArray {
            val keys = JSONObject()
            allRotatedKeys.forEach { signingKey -> keys.put(signingKey.keyId, signingKey.keyPayload) }
            val roles = JSONObject()
                .put("root", role(rotatedRootKeys.map(SigningKey::keyId), 2))
                .put("targets", role(listOf(rotatedTargetsKey.keyId), 1))
                .put("snapshot", role(listOf(rotatedSnapshotKey.keyId), 1))
                .put("timestamp", role(listOf(rotatedTimestampKey.keyId), 1))
            val signed = JSONObject()
                .put("_type", "root")
                .put("spec_version", "1.0.0")
                .put("consistent_snapshot", true)
                .put("version", version)
                .put("expires", Instant.ofEpochSecond(NOW + 3_600).toString())
                .put("keys", keys)
                .put("roles", roles)
            val canonicalSigned = canonicalTufJson(signed)
            val authorizingKeys = buildList {
                if (includeCurrentRootSignatures) addAll(rootKeys)
                if (includeCandidateRootSignatures) addAll(rotatedRootKeys)
            }
            val signatures = JSONArray()
            authorizingKeys.forEach { signingKey ->
                signatures.put(
                    JSONObject()
                        .put("keyid", signingKey.keyId)
                        .put("sig", sign(signingKey.pair, canonicalSigned).toHex()),
                )
            }
            return JSONObject().put("signed", signed).put("signatures", signatures).toString().toByteArray()
        }

        fun metadata(
            release: AndroidUpdateRelease,
            releaseIdentity: AndroidReleaseIdentity,
            timestampVersion: Long = 1,
            snapshotVersion: Long = 1,
            targetsVersion: Long = 1,
            includeUnrelatedTarget: Boolean = false,
        ): MetadataBundle {
            val artifact = release.artifact
            val targetIdentity = JSONObject()
                .put("product", "android")
                .put("distribution", releaseIdentity.distribution)
                .put("release_namespace", releaseIdentity.releaseNamespace)
                .put("oem_id", releaseIdentity.oemId ?: JSONObject.NULL)
                .put("channel", "stable")
                .put("os", "android")
                .put("architecture", "aarch64")
            val custom = JSONObject().put(
                "pixels",
                JSONObject()
                    .put("schema_version", 1)
                    .put("target", targetIdentity)
                    .put("build_number", artifact.buildNumber)
                    .put("version", artifact.version)
                    .put("platform_signer_sha256", artifact.platformSignerSha256),
            )
            val target = JSONObject()
                .put("length", artifact.sizeBytes)
                .put("hashes", JSONObject().put("sha256", artifact.sha256))
                .put("custom", custom)
            val targets = JSONObject().put(artifact.targetName, target)
            if (includeUnrelatedTarget) {
                targets.put(
                    "android/android/official/stable/aarch64/1/unrelated.apk",
                    JSONObject()
                        .put("length", 1)
                        .put("hashes", JSONObject().put("sha256", "e".repeat(64)))
                        .put("custom", JSONObject()),
                )
            }
            val targetsSigned = commonMetadata("targets", targetsVersion)
                .put("targets", targets)
            val targetsBytes = signedEnvelope(targetsSigned, targetsKey)
            val snapshotSigned = commonMetadata("snapshot", snapshotVersion)
                .put("meta", JSONObject().put("targets.json", metadataDescription(targetsBytes, targetsVersion)))
            val snapshotBytes = signedEnvelope(snapshotSigned, snapshotKey)
            val timestampSigned = commonMetadata("timestamp", timestampVersion)
                .put("meta", JSONObject().put("snapshot.json", metadataDescription(snapshotBytes, snapshotVersion)))
            return MetadataBundle(signedEnvelope(timestampSigned, timestampKey), snapshotBytes, targetsBytes)
        }

        private fun commonMetadata(roleName: String, version: Long): JSONObject = JSONObject()
            .put("_type", roleName)
            .put("spec_version", "1.0.0")
            .put("version", version)
            .put("expires", Instant.ofEpochSecond(NOW + 3_600).toString())

        private fun metadataDescription(metadataBytes: ByteArray, version: Long): JSONObject = JSONObject()
            .put("length", metadataBytes.size)
            .put("hashes", JSONObject().put("sha256", MessageDigest.getInstance("SHA-256").digest(metadataBytes).toHex()))
            .put("version", version)

        private fun signedEnvelope(signed: JSONObject, signingKey: SigningKey): ByteArray {
            val signature = JSONObject()
                .put("keyid", signingKey.keyId)
                .put("sig", sign(signingKey.pair, canonicalTufJson(signed)).toHex())
            return JSONObject().put("signed", signed).put("signatures", JSONArray().put(signature)).toString().toByteArray()
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

        fun androidRelease(): AndroidUpdateRelease = AndroidUpdateRelease(
            releaseId = "11111111-1111-4111-8111-111111111111",
            repositoryPublicationSha256 = "a".repeat(64),
            repositoryRootVersion = 1,
            revision = 1,
            createdAtEpochMillis = 1_800_000_000_000,
            updatedAtEpochMillis = 1_800_000_000_000,
            artifact = AndroidUpdateArtifact(
                buildNumber = 25,
                version = "1.0.25",
                metadataBaseUrl = "https://updates.example.test/metadata/",
                targetsBaseUrl = "https://updates.example.test/targets/",
                targetName = "android/android/official/stable/aarch64/25/pixels.apk",
                sha256 = "b".repeat(64),
                platformSignerSha256 = "d".repeat(64),
                sizeBytes = 4096,
            ),
        )
    }
}
