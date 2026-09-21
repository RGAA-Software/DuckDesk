package yun.pixels.client.core.network

import java.security.MessageDigest
import yun.pixels.client.core.domain.update.AndroidTufMetadataWatermark
import yun.pixels.client.core.domain.update.AndroidTufTrustedRoot
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootState
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootStore
import yun.pixels.client.core.domain.update.AndroidUpdateRelease

class AndroidTufTrustedRootManager private constructor(
    private var currentRoot: VerifiedTufRoot,
    private var currentRootBytes: ByteArray,
    private var metadataWatermark: AndroidTufMetadataWatermark?,
    private val releaseIdentity: AndroidReleaseIdentity,
    private val trustedRootStore: AndroidTufTrustedRootStore,
    private val rootVerifier: AndroidTufRootVerifier,
    private val metadataVerifier: AndroidTufMetadataVerifier,
) {
    private val lock = Any()

    val currentVersion: Long
        get() = synchronized(lock) { currentRoot.version }

    fun acceptNextRoot(
        candidateRootBytes: ByteArray,
        nowEpochSeconds: Long = System.currentTimeMillis() / 1_000,
    ): Boolean =
        synchronized(lock) {
            val verifiedCandidate = rootVerifier.verifyNextRoot(currentRoot, candidateRootBytes, nowEpochSeconds)
                ?: return@synchronized false
            val persistentRoot = releaseIdentity.toPersistentRoot(
                verifiedCandidate.version,
                candidateRootBytes,
                metadataWatermark,
            )
            if (!trustedRootStore.save(persistentRoot)) return@synchronized false
            currentRoot = verifiedCandidate
            currentRootBytes = candidateRootBytes.copyOf()
            true
        }

    fun verifyAndCommitMetadata(
        release: AndroidUpdateRelease,
        timestampBytes: ByteArray,
        snapshotBytes: ByteArray,
        targetsBytes: ByteArray,
        nowEpochSeconds: Long = System.currentTimeMillis() / 1_000,
    ): Boolean = synchronized(lock) {
        val verifiedVersions = metadataVerifier.verify(
            currentRoot,
            releaseIdentity,
            release,
            timestampBytes,
            snapshotBytes,
            targetsBytes,
            nowEpochSeconds,
        ) ?: return@synchronized false
        val candidateWatermark = AndroidTufMetadataWatermark(
            verifiedVersions.timestamp,
            timestampBytes.sha256Hex(),
            verifiedVersions.snapshot,
            snapshotBytes.sha256Hex(),
            verifiedVersions.targets,
            targetsBytes.sha256Hex(),
        )
        if (!candidateWatermark.isAtLeast(metadataWatermark)) return@synchronized false
        val persistentRoot = releaseIdentity.toPersistentRoot(currentRoot.version, currentRootBytes, candidateWatermark)
        if (!trustedRootStore.save(persistentRoot)) return@synchronized false
        metadataWatermark = candidateWatermark
        true
    }

    companion object {
        fun create(
            trustConfiguration: AndroidTufTrustConfiguration,
            releaseIdentity: AndroidReleaseIdentity,
            trustedRootStore: AndroidTufTrustedRootStore,
            nowEpochSeconds: Long = System.currentTimeMillis() / 1_000,
        ): AndroidTufTrustedRootManager? = create(
            trustConfiguration,
            releaseIdentity,
            trustedRootStore,
            nowEpochSeconds,
            JcaEd25519Verifier.android(),
        )

        internal fun create(
            trustConfiguration: AndroidTufTrustConfiguration,
            releaseIdentity: AndroidReleaseIdentity,
            trustedRootStore: AndroidTufTrustedRootStore,
            nowEpochSeconds: Long,
            signatureVerifier: DeploymentSignatureVerifier,
        ): AndroidTufTrustedRootManager? {
            val rootVerifier = AndroidTufRootVerifier(signatureVerifier)
            val embeddedRoot = trustConfiguration.verifiedInitialRoot
            val selectedState = when (val storedState = trustedRootStore.load()) {
                AndroidTufTrustedRootState.Empty -> {
                    val initialState = releaseIdentity.toPersistentRoot(
                        embeddedRoot.version,
                        trustConfiguration.initialRootBytes,
                        null,
                    )
                    if (!trustedRootStore.save(initialState)) return null
                    SelectedTrustedRoot(embeddedRoot, trustConfiguration.initialRootBytes.copyOf(), null)
                }
                AndroidTufTrustedRootState.Invalid -> return null
                is AndroidTufTrustedRootState.Present -> selectTrustedRoot(
                    storedState.trustedRoot,
                    embeddedRoot,
                    trustConfiguration.initialRootBytes,
                    releaseIdentity,
                    trustedRootStore,
                    rootVerifier,
                    nowEpochSeconds,
                ) ?: return null
            }
            return AndroidTufTrustedRootManager(
                selectedState.verifiedRoot,
                selectedState.rootBytes,
                selectedState.metadataWatermark,
                releaseIdentity,
                trustedRootStore,
                rootVerifier,
                AndroidTufMetadataVerifier(signatureVerifier),
            )
        }

        private fun selectTrustedRoot(
            storedRootState: AndroidTufTrustedRoot,
            embeddedRoot: VerifiedTufRoot,
            embeddedRootBytes: ByteArray,
            releaseIdentity: AndroidReleaseIdentity,
            trustedRootStore: AndroidTufTrustedRootStore,
            rootVerifier: AndroidTufRootVerifier,
            nowEpochSeconds: Long,
        ): SelectedTrustedRoot? {
            if (!releaseIdentity.matches(storedRootState)) return null
            val storedRoot = rootVerifier.verifyInitialRoot(
                storedRootState.copyRootBytes(),
                nowEpochSeconds,
            ) ?: return null
            if (storedRoot.version != storedRootState.version) return null
            return when {
                storedRoot.version > embeddedRoot.version -> SelectedTrustedRoot(
                    storedRoot,
                    storedRootState.copyRootBytes(),
                    storedRootState.metadataWatermark,
                )
                storedRoot.version == embeddedRoot.version -> storedRoot
                    .takeIf { it.hasSameDefinition(embeddedRoot) }
                    ?.let {
                        SelectedTrustedRoot(
                            storedRoot,
                            storedRootState.copyRootBytes(),
                            storedRootState.metadataWatermark,
                        )
                    }
                else -> {
                    val advancedRoot = rootVerifier.verifyNextRoot(
                        storedRoot,
                        embeddedRootBytes,
                        nowEpochSeconds,
                    ) ?: return null
                    val advancedState = releaseIdentity.toPersistentRoot(
                        advancedRoot.version,
                        embeddedRootBytes,
                        storedRootState.metadataWatermark,
                    )
                    SelectedTrustedRoot(
                        advancedRoot,
                        embeddedRootBytes.copyOf(),
                        storedRootState.metadataWatermark,
                    ).takeIf { trustedRootStore.save(advancedState) }
                }
            }
        }

        private fun AndroidReleaseIdentity.matches(trustedRoot: AndroidTufTrustedRoot): Boolean =
            distribution == trustedRoot.distribution &&
                releaseNamespace == trustedRoot.releaseNamespace &&
                oemId == trustedRoot.oemId

        private fun AndroidReleaseIdentity.toPersistentRoot(
            version: Long,
            rootBytes: ByteArray,
            metadataWatermark: AndroidTufMetadataWatermark?,
        ): AndroidTufTrustedRoot =
            AndroidTufTrustedRoot(distribution, releaseNamespace, oemId, version, rootBytes, metadataWatermark)

        private fun VerifiedTufRoot.hasSameDefinition(other: VerifiedTufRoot): Boolean =
            canonicalSigned.contentEquals(other.canonicalSigned)

        private fun AndroidTufMetadataWatermark.isAtLeast(current: AndroidTufMetadataWatermark?): Boolean {
            current ?: return true
            return timestampVersion >= current.timestampVersion &&
                (timestampVersion > current.timestampVersion || timestampSha256 == current.timestampSha256) &&
                snapshotVersion >= current.snapshotVersion &&
                (snapshotVersion > current.snapshotVersion || snapshotSha256 == current.snapshotSha256) &&
                targetsVersion >= current.targetsVersion &&
                (targetsVersion > current.targetsVersion || targetsSha256 == current.targetsSha256)
        }

        private fun ByteArray.sha256Hex(): String =
            MessageDigest.getInstance("SHA-256").digest(this).joinToString("") { byte ->
                "%02x".format(byte.toInt() and 0xff)
            }
    }
}

private data class SelectedTrustedRoot(
    val verifiedRoot: VerifiedTufRoot,
    val rootBytes: ByteArray,
    val metadataWatermark: AndroidTufMetadataWatermark?,
)
