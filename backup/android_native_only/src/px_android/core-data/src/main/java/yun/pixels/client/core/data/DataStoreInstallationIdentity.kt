package yun.pixels.client.core.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStoreFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import yun.pixels.client.core.domain.session.InstallationIdentity
import java.util.UUID

class DataStoreInstallationIdentity private constructor(
    private val dataStore: DataStore<Preferences>,
) : InstallationIdentity {
    private val lock = Mutex()

    override suspend fun value(): String = lock.withLock {
        dataStore.data.first()[installationIdKey] ?: UUID.randomUUID().toString().also { generated ->
            dataStore.edit { preferences -> preferences[installationIdKey] = generated }
        }
    }

    companion object {
        private val installationIdKey = stringPreferencesKey("installation_id")

        fun create(context: Context, scope: CoroutineScope): DataStoreInstallationIdentity {
            val applicationContext = context.applicationContext
            val store = androidx.datastore.preferences.core.PreferenceDataStoreFactory.create(
                scope = scope,
                produceFile = { applicationContext.preferencesDataStoreFile("pixels_installation") },
            )
            return DataStoreInstallationIdentity(store)
        }
    }
}
