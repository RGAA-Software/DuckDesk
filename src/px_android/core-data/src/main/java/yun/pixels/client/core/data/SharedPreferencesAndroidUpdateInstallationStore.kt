package yun.pixels.client.core.data

import android.content.Context
import android.content.SharedPreferences
import java.util.UUID
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationPhase
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationRecord
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationState
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationStore

class SharedPreferencesAndroidUpdateInstallationStore private constructor(
    private val preferences: SharedPreferences,
    private val cipher: AndroidKeystoreTextCipher,
) : AndroidUpdateInstallationStore {
    private val lock = Any()

    override fun load(): AndroidUpdateInstallationState = synchronized(lock) {
        if (preferences.all.isEmpty()) return@synchronized AndroidUpdateInstallationState.Empty
        if (preferences.all.keys != setOf(ENCRYPTED_STATE)) return@synchronized AndroidUpdateInstallationState.Invalid
        val encryptedState = preferences.getString(ENCRYPTED_STATE, null)
            ?: return@synchronized AndroidUpdateInstallationState.Invalid
        val serializedState = cipher.decrypt(encryptedState)
            ?: return@synchronized AndroidUpdateInstallationState.Invalid
        AndroidUpdateInstallationCodec.decode(serializedState)?.let(AndroidUpdateInstallationState::Present)
            ?: AndroidUpdateInstallationState.Invalid
    }

    override fun save(record: AndroidUpdateInstallationRecord): Boolean = synchronized(lock) {
        val serializedState = AndroidUpdateInstallationCodec.encode(record) ?: return@synchronized false
        val encryptedState = runCatching { cipher.encrypt(serializedState) }.getOrNull() ?: return@synchronized false
        preferences.edit().clear().putString(ENCRYPTED_STATE, encryptedState).commit()
    }

    override fun clear(): Boolean = synchronized(lock) { preferences.edit().clear().commit() }

    companion object {
        fun create(context: Context): SharedPreferencesAndroidUpdateInstallationStore =
            SharedPreferencesAndroidUpdateInstallationStore(
                context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE),
                AndroidKeystoreTextCipher(KEY_ALIAS),
            )

        private const val PREFERENCES_NAME = "pixels_android_update_installation_v1"
        private const val KEY_ALIAS = "pixels_android_update_installation_v1"
        private const val ENCRYPTED_STATE = "encrypted_state"
    }
}

internal object AndroidUpdateInstallationCodec {
    fun encode(record: AndroidUpdateInstallationRecord): String? {
        if (!valid(record)) return null
        return listOf(
            SCHEMA_VERSION,
            record.releaseId,
            record.targetBuildNumber.toString(),
            record.artifactSha256,
            record.sessionId.toString(),
            record.phase.name,
            record.failureStatus?.toString().orEmpty(),
        ).joinToString("\n")
    }

    fun decode(serializedState: String): AndroidUpdateInstallationRecord? {
        val fields = serializedState.split('\n')
        if (fields.size != FIELD_COUNT || fields[0] != SCHEMA_VERSION) return null
        val record = AndroidUpdateInstallationRecord(
            releaseId = fields[1],
            targetBuildNumber = fields[2].toLongOrNull() ?: return null,
            artifactSha256 = fields[3],
            sessionId = fields[4].toIntOrNull() ?: return null,
            phase = runCatching { AndroidUpdateInstallationPhase.valueOf(fields[5]) }.getOrNull() ?: return null,
            failureStatus = fields[6].takeIf(String::isNotEmpty)?.toIntOrNull(),
        )
        if (fields[6].isNotEmpty() && record.failureStatus == null) return null
        return record.takeIf(::valid)
    }

    private fun valid(record: AndroidUpdateInstallationRecord): Boolean =
        runCatching { UUID.fromString(record.releaseId).toString() == record.releaseId }.getOrDefault(false) &&
            record.releaseId != NIL_UUID && record.targetBuildNumber > 0 && record.sessionId >= 0 &&
            record.artifactSha256.length == 64 && record.artifactSha256.all { character ->
                character in '0'..'9' || character in 'a'..'f'
            } &&
            (record.phase == AndroidUpdateInstallationPhase.Failed) == (record.failureStatus != null)

    private const val SCHEMA_VERSION = "1"
    private const val FIELD_COUNT = 7
    private const val NIL_UUID = "00000000-0000-0000-0000-000000000000"
}
