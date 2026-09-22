package yun.pixels.client.update

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.pm.PackageInstaller
import android.os.Build
import yun.pixels.client.core.data.SharedPreferencesAndroidUpdateInstallationStore
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationPhase
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationRecord
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationState
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationStore

class AndroidInstallResultReceiver : BroadcastReceiver() {
    override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != "${context.packageName}.UPDATE_INSTALL_RESULT") return
        val expectedSessionId = intent.getIntExtra(EXTRA_EXPECTED_SESSION_ID, -1)
        val installationStore = SharedPreferencesAndroidUpdateInstallationStore.create(context)
        val state = installationStore.load() as? AndroidUpdateInstallationState.Present ?: return
        if (expectedSessionId < 0 || state.record.sessionId != expectedSessionId) return
        when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
            PackageInstaller.STATUS_PENDING_USER_ACTION -> handleUserApproval(context, intent, installationStore, state.record)
            PackageInstaller.STATUS_SUCCESS -> installationStore.save(
                state.record.copy(phase = AndroidUpdateInstallationPhase.AppliedAwaitingReconcile, failureStatus = null),
            )
            else -> installationStore.save(
                state.record.copy(phase = AndroidUpdateInstallationPhase.Failed, failureStatus = status),
            )
        }
    }

    private fun handleUserApproval(
        context: Context,
        resultIntent: Intent,
        installationStore: AndroidUpdateInstallationStore,
        record: AndroidUpdateInstallationRecord,
    ) {
        val confirmationIntent = resultIntent.installationConfirmationIntent() ?: run {
            installationStore.save(record.copy(phase = AndroidUpdateInstallationPhase.Failed, failureStatus = LOCAL_CONFIRMATION_FAILURE))
            return
        }
        if (!installationStore.save(record.copy(phase = AndroidUpdateInstallationPhase.AwaitingUserApproval, failureStatus = null))) return
        confirmationIntent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
        runCatching { context.startActivity(confirmationIntent) }.onFailure {
            installationStore.save(record.copy(phase = AndroidUpdateInstallationPhase.Failed, failureStatus = LOCAL_CONFIRMATION_FAILURE))
        }
    }
}

@Suppress("DEPRECATION")
private fun Intent.installationConfirmationIntent(): Intent? = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
    getParcelableExtra(Intent.EXTRA_INTENT, Intent::class.java)
} else {
    getParcelableExtra(Intent.EXTRA_INTENT)
}

private const val LOCAL_CONFIRMATION_FAILURE = -1_002
