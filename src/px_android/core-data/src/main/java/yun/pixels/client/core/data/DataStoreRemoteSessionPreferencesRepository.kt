package yun.pixels.client.core.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.booleanPreferencesKey
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.intPreferencesKey
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStoreFile
import java.io.IOException
import java.security.MessageDigest
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import yun.pixels.client.core.domain.session.RemoteInputMode
import yun.pixels.client.core.domain.session.RemoteSessionPreferences
import yun.pixels.client.core.domain.session.RemoteSessionPreferencesRepository

class DataStoreRemoteSessionPreferencesRepository private constructor(
    private val dataStore: DataStore<Preferences>,
) : RemoteSessionPreferencesRepository {
    override suspend fun load(deviceKey: String): RemoteSessionPreferences {
        val keyPrefix = deviceKey.storageKeyPrefix()
        val stored = dataStore.data
            .catch { error -> if (error is IOException) emit(emptyPreferences()) else throw error }
            .first()
        return decodeRemoteSessionPreferences(
            frameRate = stored[intPreferencesKey("${keyPrefix}_fps")],
            audioEnabled = stored[booleanPreferencesKey("${keyPrefix}_audio")],
            inputMode = stored[stringPreferencesKey("${keyPrefix}_input")],
        )
    }

    override suspend fun save(deviceKey: String, preferences: RemoteSessionPreferences) {
        val keyPrefix = deviceKey.storageKeyPrefix()
        dataStore.edit { stored ->
            stored[intPreferencesKey("${keyPrefix}_fps")] = preferences.frameRate
            stored[booleanPreferencesKey("${keyPrefix}_audio")] = preferences.audioEnabled
            stored[stringPreferencesKey("${keyPrefix}_input")] = preferences.inputMode.name
        }
    }

    companion object {
        fun create(context: Context, scope: CoroutineScope): DataStoreRemoteSessionPreferencesRepository {
            val applicationContext = context.applicationContext
            val store = androidx.datastore.preferences.core.PreferenceDataStoreFactory.create(
                scope = scope,
                produceFile = { applicationContext.preferencesDataStoreFile("pixels_remote_session_preferences") },
            )
            return DataStoreRemoteSessionPreferencesRepository(store)
        }
    }
}

internal fun decodeRemoteSessionPreferences(
    frameRate: Int?,
    audioEnabled: Boolean?,
    inputMode: String?,
): RemoteSessionPreferences = RemoteSessionPreferences(
    frameRate = frameRate?.takeIf { it in RemoteSessionPreferences.SUPPORTED_FRAME_RATES } ?: RemoteSessionPreferences.DEFAULT_FRAME_RATE,
    audioEnabled = audioEnabled ?: true,
    inputMode = inputMode?.let { stored -> runCatching { RemoteInputMode.valueOf(stored) }.getOrNull() } ?: RemoteInputMode.DirectTouch,
)

private fun String.storageKeyPrefix(): String = MessageDigest.getInstance("SHA-256")
    .digest(encodeToByteArray())
    .joinToString(separator = "") { byte -> "%02x".format(byte.toInt() and 0xFF) }
