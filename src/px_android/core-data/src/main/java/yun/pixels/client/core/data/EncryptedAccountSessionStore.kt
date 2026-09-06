package yun.pixels.client.core.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStoreFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.first
import org.json.JSONObject
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountSessionStore
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import java.io.IOException

class EncryptedAccountSessionStore private constructor(
    private val dataStore: DataStore<Preferences>,
    private val cipher: AndroidKeystoreTextCipher,
) : AccountSessionStore {
    override suspend fun load(): AccountSession? {
        val encrypted = dataStore.data
            .catch { error -> if (error is IOException) emit(emptyPreferences()) else throw error }
            .first()[sessionKey]
            ?: return null
        return cipher.decrypt(encrypted)?.let(::decode)
    }

    override suspend fun save(session: AccountSession) {
        val encrypted = cipher.encrypt(encode(session))
        dataStore.edit { preferences -> preferences[sessionKey] = encrypted }
    }

    override suspend fun clear() {
        dataStore.edit { preferences -> preferences.remove(sessionKey) }
    }

    companion object {
        private val sessionKey = stringPreferencesKey("account_session_v1")

        fun create(context: Context, scope: CoroutineScope): EncryptedAccountSessionStore {
            val applicationContext = context.applicationContext
            val store = androidx.datastore.preferences.core.PreferenceDataStoreFactory.create(
                scope = scope,
                produceFile = { applicationContext.preferencesDataStoreFile("pixels_account") },
            )
            return EncryptedAccountSessionStore(store, AndroidKeystoreTextCipher("pixels_account_session_v1"))
        }

        private fun encode(session: AccountSession): String = JSONObject()
            .put("endpoint", session.endpoint.baseUrl)
            .put("uid", session.profile.userId)
            .put("username", session.profile.username)
            .put("avatar", session.profile.avatarPath ?: JSONObject.NULL)
            .put("mustChangePassword", session.profile.mustChangePassword)
            .put("accessToken", session.accessToken)
            .put("expiresAt", session.expiresAtEpochMillis)
            .put("absoluteExpiresAt", session.absoluteExpiresAtEpochMillis)
            .toString()

        private fun decode(value: String): AccountSession? = runCatching {
            val json = JSONObject(value)
            AccountSession(
                endpoint = ConsoleEndpoint(json.getString("endpoint")),
                profile = AccountProfile(
                    userId = json.getString("uid"),
                    username = json.getString("username"),
                    avatarPath = json.optString("avatar").takeIf(String::isNotBlank),
                    mustChangePassword = json.optBoolean("mustChangePassword"),
                ),
                accessToken = json.getString("accessToken"),
                expiresAtEpochMillis = json.getLong("expiresAt"),
                absoluteExpiresAtEpochMillis = json.getLong("absoluteExpiresAt"),
            )
        }.getOrNull()
    }

}
