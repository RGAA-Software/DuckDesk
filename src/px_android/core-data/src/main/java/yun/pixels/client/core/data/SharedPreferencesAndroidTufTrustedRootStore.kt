package yun.pixels.client.core.data

import android.content.Context
import android.content.SharedPreferences
import java.util.Base64
import yun.pixels.client.core.domain.update.AndroidTufTrustedRoot
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootState
import yun.pixels.client.core.domain.update.AndroidTufTrustedRootStore

class SharedPreferencesAndroidTufTrustedRootStore private constructor(
    private val preferences: SharedPreferences,
    private val cipher: AndroidKeystoreTextCipher,
) : AndroidTufTrustedRootStore {
    private val lock = Any()

    override fun load(): AndroidTufTrustedRootState = synchronized(lock) {
        if (preferences.all.isEmpty()) return@synchronized AndroidTufTrustedRootState.Empty
        if (preferences.all.keys != setOf(ENCRYPTED_STATE)) return@synchronized AndroidTufTrustedRootState.Invalid
        val encryptedState = preferences.getString(ENCRYPTED_STATE, null)
            ?: return@synchronized AndroidTufTrustedRootState.Invalid
        val serializedState = cipher.decrypt(encryptedState)
            ?: return@synchronized AndroidTufTrustedRootState.Invalid
        TufTrustedRootStateCodec.decode(serializedState)?.let(AndroidTufTrustedRootState::Present)
            ?: AndroidTufTrustedRootState.Invalid
    }

    override fun save(trustedRoot: AndroidTufTrustedRoot): Boolean = synchronized(lock) {
        val serializedState = TufTrustedRootStateCodec.encode(trustedRoot) ?: return@synchronized false
        val encryptedState = runCatching { cipher.encrypt(serializedState) }.getOrNull() ?: return@synchronized false
        preferences.edit().clear().putString(ENCRYPTED_STATE, encryptedState).commit()
    }

    companion object {
        fun create(context: Context): SharedPreferencesAndroidTufTrustedRootStore =
            SharedPreferencesAndroidTufTrustedRootStore(
                context.applicationContext.getSharedPreferences(PREFERENCES_NAME, Context.MODE_PRIVATE),
                AndroidKeystoreTextCipher(KEY_ALIAS),
            )

        private const val PREFERENCES_NAME = "pixels_android_tuf_trusted_root_v1"
        private const val KEY_ALIAS = "pixels_android_tuf_trusted_root_v1"
        private const val ENCRYPTED_STATE = "encrypted_state"
    }
}

internal object TufTrustedRootStateCodec {
    fun encode(trustedRoot: AndroidTufTrustedRoot): String? {
        if (!validReleaseDomain(trustedRoot.distribution, trustedRoot.releaseNamespace, trustedRoot.oemId)) return null
        val trustedRootBytes = trustedRoot.copyRootBytes()
        if (
            trustedRoot.version <= 0 || trustedRootBytes.isEmpty() ||
            trustedRootBytes.size > MAXIMUM_ROOT_BYTES
        ) {
            return null
        }
        return listOf(
            SCHEMA_VERSION,
            trustedRoot.distribution,
            trustedRoot.releaseNamespace,
            trustedRoot.oemId.orEmpty(),
            trustedRoot.version.toString(),
            Base64.getEncoder().encodeToString(trustedRootBytes),
        ).joinToString("\n")
    }

    fun decode(serializedState: String): AndroidTufTrustedRoot? {
        val stateFields = serializedState.split('\n')
        if (stateFields.size != FIELD_COUNT || stateFields[0] != SCHEMA_VERSION) return null
        val distribution = stateFields[1]
        val releaseNamespace = stateFields[2]
        val oemId = stateFields[3].ifEmpty { null }
        if (!validReleaseDomain(distribution, releaseNamespace, oemId)) return null
        val version = stateFields[4].toLongOrNull()?.takeIf { candidateVersion -> candidateVersion > 0 } ?: return null
        val encodedRoot = stateFields[5]
        val rootBytes = runCatching { Base64.getDecoder().decode(encodedRoot) }.getOrNull()
            ?.takeIf { candidateBytes -> candidateBytes.isNotEmpty() && candidateBytes.size <= MAXIMUM_ROOT_BYTES }
            ?: return null
        if (Base64.getEncoder().encodeToString(rootBytes) != encodedRoot) return null
        return AndroidTufTrustedRoot(distribution, releaseNamespace, oemId, version, rootBytes)
    }

    private fun validReleaseDomain(
        distribution: String,
        releaseNamespace: String,
        oemId: String?,
    ): Boolean = when (distribution) {
        "official" -> releaseNamespace == "pixels.official" && oemId == null
        "customer" -> releaseNamespace == "pixels.customer" && oemId == null
        "oem" -> oemId?.let { candidateOemId ->
            candidateOemId.length in 3..32 && candidateOemId.first() != '-' && candidateOemId.last() != '-' &&
                "--" !in candidateOemId && candidateOemId !in RESERVED_OEM_IDS &&
                candidateOemId.all { character ->
                    character in 'a'..'z' || character in '0'..'9' || character == '-'
                } &&
                releaseNamespace == "oem.$candidateOemId"
        } == true
        else -> false
    }

    private const val SCHEMA_VERSION = "1"
    private const val FIELD_COUNT = 6
    private const val MAXIMUM_ROOT_BYTES = 1024 * 1024
    private val RESERVED_OEM_IDS = setOf("pixels", "official", "customer", "oem")
}
