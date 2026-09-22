package yun.pixels.client.core.domain.update

import yun.pixels.client.core.domain.account.AccountResult

data class AndroidUpdateArtifact(
    val buildNumber: Long,
    val version: String,
    val metadataBaseUrl: String,
    val targetsBaseUrl: String,
    val targetName: String,
    val sha256: String,
    val platformSignerSha256: String,
    val sizeBytes: Long,
)

data class AndroidUpdateRelease(
    val releaseId: String,
    val repositoryPublicationSha256: String,
    val repositoryRootVersion: Long,
    val revision: Long,
    val createdAtEpochMillis: Long,
    val updatedAtEpochMillis: Long,
    val artifact: AndroidUpdateArtifact,
)

data class PreparedAndroidUpdate(
    val release: AndroidUpdateRelease,
    val stagedApkPath: String,
)

interface AndroidUpdateRepository {
    suspend fun latest(): AccountResult<AndroidUpdateRelease>
}

interface AndroidUpdatePreparationRepository : AndroidUpdateRepository {
    suspend fun prepare(releaseId: String): AccountResult<PreparedAndroidUpdate>
}

fun interface PreparedAndroidUpdateVerifier {
    fun verify(preparedUpdate: PreparedAndroidUpdate): Boolean
}

interface AndroidUpdateInstaller {
    fun currentInstallationState(): AndroidUpdateInstallationState

    suspend fun install(preparedUpdate: PreparedAndroidUpdate): AccountResult<Unit>
}

enum class AndroidUpdateInstallationPhase {
    Submitted,
    AwaitingUserApproval,
    AppliedAwaitingReconcile,
    Installed,
    Failed,
}

data class AndroidUpdateInstallationRecord(
    val releaseId: String,
    val targetBuildNumber: Long,
    val artifactSha256: String,
    val sessionId: Int,
    val phase: AndroidUpdateInstallationPhase,
    val failureStatus: Int?,
)

sealed interface AndroidUpdateInstallationState {
    data object Empty : AndroidUpdateInstallationState

    data object Invalid : AndroidUpdateInstallationState

    data class Present(val record: AndroidUpdateInstallationRecord) : AndroidUpdateInstallationState
}

interface AndroidUpdateInstallationStore {
    fun load(): AndroidUpdateInstallationState

    fun save(record: AndroidUpdateInstallationRecord): Boolean

    fun clear(): Boolean
}
