package yun.pixels.client.core.network

import yun.pixels.client.core.domain.update.AndroidTufTrustedRoot
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootState
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootStore

class AndroidTufTrustedRootManager private constructor(
    private var currentRoot: VerifiedTufRoot,
    private val releaseIdentity: AndroidReleaseIdentity,
    private val trustedRootStore: AndroidTufTrustedRootStore,
    private val rootVerifier: AndroidTufRootVerifier,
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
            val persistentRoot = releaseIdentity.toPersistentRoot(verifiedCandidate.version, candidateRootBytes)
            if (!trustedRootStore.save(persistentRoot)) return@synchronized false
            currentRoot = verifiedCandidate
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
            val selectedRoot = when (val storedState = trustedRootStore.load()) {
                AndroidTufTrustedRootState.Empty -> {
                    val initialState = releaseIdentity.toPersistentRoot(
                        embeddedRoot.version,
                        trustConfiguration.initialRootBytes,
                    )
                    if (!trustedRootStore.save(initialState)) return null
                    embeddedRoot
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
            return AndroidTufTrustedRootManager(selectedRoot, releaseIdentity, trustedRootStore, rootVerifier)
        }

        private fun selectTrustedRoot(
            storedRootState: AndroidTufTrustedRoot,
            embeddedRoot: VerifiedTufRoot,
            embeddedRootBytes: ByteArray,
            releaseIdentity: AndroidReleaseIdentity,
            trustedRootStore: AndroidTufTrustedRootStore,
            rootVerifier: AndroidTufRootVerifier,
            nowEpochSeconds: Long,
        ): VerifiedTufRoot? {
            if (!releaseIdentity.matches(storedRootState)) return null
            val storedRoot = rootVerifier.verifyInitialRoot(storedRootState.copyRootBytes(), nowEpochSeconds) ?: return null
            if (storedRoot.version != storedRootState.version) return null
            return when {
                storedRoot.version > embeddedRoot.version -> storedRoot
                storedRoot.version == embeddedRoot.version -> storedRoot.takeIf { it.hasSameDefinition(embeddedRoot) }
                else -> {
                    val advancedRoot = rootVerifier.verifyNextRoot(
                        storedRoot,
                        embeddedRootBytes,
                        nowEpochSeconds,
                    ) ?: return null
                    val advancedState = releaseIdentity.toPersistentRoot(advancedRoot.version, embeddedRootBytes)
                    advancedRoot.takeIf { trustedRootStore.save(advancedState) }
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
        ): AndroidTufTrustedRoot =
            AndroidTufTrustedRoot(distribution, releaseNamespace, oemId, version, rootBytes.copyOf())

        private fun VerifiedTufRoot.hasSameDefinition(other: VerifiedTufRoot): Boolean =
            canonicalSigned.contentEquals(other.canonicalSigned)
    }
}
