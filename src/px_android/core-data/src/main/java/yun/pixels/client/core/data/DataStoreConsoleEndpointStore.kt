package yun.pixels.client.core.data

import android.content.Context
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStoreFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.first
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ConsoleEndpointStore

class DataStoreConsoleEndpointStore private constructor(
    private val dataStore: DataStore<Preferences>,
) : ConsoleEndpointStore {
    override suspend fun load(): ConsoleEndpoint? = dataStore.data.first()[endpointKey]
        ?.takeIf(String::isNotBlank)
        ?.let(::ConsoleEndpoint)

    override suspend fun save(endpoint: ConsoleEndpoint) {
        dataStore.edit { preferences -> preferences[endpointKey] = endpoint.baseUrl }
    }

    override suspend fun clear() {
        dataStore.edit { preferences -> preferences.remove(endpointKey) }
    }

    companion object {
        private val endpointKey = stringPreferencesKey("console_endpoint_v1")

        fun create(context: Context, scope: CoroutineScope): DataStoreConsoleEndpointStore {
            val applicationContext = context.applicationContext
            val store = androidx.datastore.preferences.core.PreferenceDataStoreFactory.create(
                scope = scope,
                produceFile = { applicationContext.preferencesDataStoreFile("pixels_console_endpoint") },
            )
            return DataStoreConsoleEndpointStore(store)
        }
    }
}
