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
