package yun.pixels.client.update

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.content.pm.PackageManager
import java.io.File
import java.security.MessageDigest
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationPhase
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationRecord
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationState
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationStore
import yun.pixels.client.core.domain.update.AndroidUpdateInstaller
import yun.pixels.client.core.domain.update.PreparedAndroidUpdate
import yun.pixels.client.core.domain.update.PreparedAndroidUpdateVerifier

class AndroidPackageInstaller(
    private val context: Context,
    private val verifier: PreparedAndroidUpdateVerifier,
    private val installationStore: AndroidUpdateInstallationStore,
    private val currentBuildNumber: Long,
    private val ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
) : AndroidUpdateInstaller {
    init {
        reconcileInstalledBuild()
    }

    override suspend fun install(preparedUpdate: PreparedAndroidUpdate): AccountResult<Unit> = withContext(ioDispatcher) {
        if (!canStartInstallation(preparedUpdate) || !verifier.verify(preparedUpdate)) {
            return@withContext AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        val packageInstaller = context.packageManager.packageInstaller
        val artifact = preparedUpdate.release.artifact
        val sessionId = runCatching {
            val parameters = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
                setAppPackageName(context.packageName)
                setInstallReason(PackageManager.INSTALL_REASON_USER)
                setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_REQUIRED)
                setSize(artifact.sizeBytes)
            }
            packageInstaller.createSession(parameters)
        }.getOrElse { return@withContext AccountResult.Failure(AccountFailure.InvalidResponse) }

        val session = runCatching { packageInstaller.openSession(sessionId) }.getOrElse {
            runCatching { packageInstaller.abandonSession(sessionId) }
            return@withContext AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        val written = session.use { openSession -> writeVerifiedApk(openSession, preparedUpdate) }
        if (!written) {
            runCatching { packageInstaller.abandonSession(sessionId) }
            return@withContext AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        val installationRecord = AndroidUpdateInstallationRecord(
            releaseId = preparedUpdate.release.releaseId,
            targetBuildNumber = artifact.buildNumber,
            artifactSha256 = artifact.sha256,
            sessionId = sessionId,
            phase = AndroidUpdateInstallationPhase.Submitted,
            failureStatus = null,
        )
        if (!installationStore.save(installationRecord)) {
            runCatching { packageInstaller.abandonSession(sessionId) }
            return@withContext AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        val committed = runCatching {
            packageInstaller.openSession(sessionId).use { commitSession ->
                commitSession.commit(resultPendingIntent(sessionId).intentSender)
            }
        }.isSuccess
        if (!committed) {
            runCatching { packageInstaller.abandonSession(sessionId) }
            saveFailure(installationRecord, LOCAL_COMMIT_FAILURE)
            return@withContext AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        AccountResult.Success(Unit)
    }

    private fun canStartInstallation(preparedUpdate: PreparedAndroidUpdate): Boolean {
        return canStartAndroidUpdateInstallation(
            installationStore.load(),
            currentBuildNumber,
            preparedUpdate.release.artifact.buildNumber,
        )
    }

    private fun writeVerifiedApk(session: PackageInstaller.Session, preparedUpdate: PreparedAndroidUpdate): Boolean = runCatching {
        val artifact = preparedUpdate.release.artifact
        val digest = MessageDigest.getInstance("SHA-256")
        var writtenBytes = 0L
        File(preparedUpdate.stagedApkPath).inputStream().use { input ->
            session.openWrite("base.apk", 0, artifact.sizeBytes).use { output ->
                val payloadBuffer = ByteArray(COPY_BUFFER_BYTES)
                while (true) {
                    val byteCount = input.read(payloadBuffer)
                    if (byteCount < 0) break
                    writtenBytes += byteCount
                    if (writtenBytes > artifact.sizeBytes) return false
                    output.write(payloadBuffer, 0, byteCount)
                    digest.update(payloadBuffer, 0, byteCount)
                }
                session.fsync(output)
            }
        }
        writtenBytes == artifact.sizeBytes && digest.digest().toHex() == artifact.sha256
    }.getOrDefault(false)

    private fun resultPendingIntent(sessionId: Int): PendingIntent {
        val resultIntent = Intent(context, AndroidInstallResultReceiver::class.java).apply {
            action = "${context.packageName}.UPDATE_INSTALL_RESULT"
            putExtra(EXTRA_EXPECTED_SESSION_ID, sessionId)
        }
        return PendingIntent.getBroadcast(
            context,
            sessionId,
            resultIntent,
            PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE,
        )
    }

    private fun reconcileInstalledBuild() {
        val state = installationStore.load() as? AndroidUpdateInstallationState.Present ?: return
        val record = state.record
        if (currentBuildNumber == record.targetBuildNumber && record.phase != AndroidUpdateInstallationPhase.Failed) {
            installationStore.save(record.copy(phase = AndroidUpdateInstallationPhase.Installed, failureStatus = null))
            return
        }
        if (
            record.phase != AndroidUpdateInstallationPhase.Failed && record.phase != AndroidUpdateInstallationPhase.Installed &&
            runCatching { context.packageManager.packageInstaller.getSessionInfo(record.sessionId) }.getOrNull() == null
        ) {
            saveFailure(record, LOCAL_SESSION_MISSING)
        }
    }

    private fun saveFailure(record: AndroidUpdateInstallationRecord, status: Int) {
        installationStore.save(record.copy(phase = AndroidUpdateInstallationPhase.Failed, failureStatus = status))
    }
}

private fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }

internal fun canStartAndroidUpdateInstallation(
    installationState: AndroidUpdateInstallationState,
    currentBuildNumber: Long,
    targetBuildNumber: Long,
): Boolean {
    if (targetBuildNumber <= currentBuildNumber) return false
    return when (installationState) {
        AndroidUpdateInstallationState.Empty -> true
        AndroidUpdateInstallationState.Invalid -> false
        is AndroidUpdateInstallationState.Present -> when (installationState.record.phase) {
            AndroidUpdateInstallationPhase.Failed -> true
            AndroidUpdateInstallationPhase.Installed -> currentBuildNumber >= installationState.record.targetBuildNumber
            AndroidUpdateInstallationPhase.Submitted,
            AndroidUpdateInstallationPhase.AwaitingUserApproval,
            AndroidUpdateInstallationPhase.AppliedAwaitingReconcile,
            -> false
        }
    }
}

internal const val EXTRA_EXPECTED_SESSION_ID = "expected_session_id"
private const val COPY_BUFFER_BYTES = 64 * 1024
private const val LOCAL_COMMIT_FAILURE = -1_001
private const val LOCAL_SESSION_MISSING = -1_003
