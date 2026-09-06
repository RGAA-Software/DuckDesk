package yun.pixels.client.core.data

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import androidx.datastore.core.DataStore
import androidx.datastore.preferences.core.Preferences
import androidx.datastore.preferences.core.edit
import androidx.datastore.preferences.core.emptyPreferences
import androidx.datastore.preferences.core.stringPreferencesKey
import androidx.datastore.preferences.preferencesDataStoreFile
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.catch
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.update
import org.json.JSONArray
import org.json.JSONObject
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.RemoteDevice
import yun.pixels.client.core.domain.device.ResolvedDevice
import java.io.IOException
import java.security.KeyStore
import java.security.MessageDigest
import java.util.Base64
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

class DeviceDataStore private constructor(
    private val dataStore: DataStore<Preferences>,
    private val credentialCipher: DeviceCredentialCipher,
) : DeviceDirectory {
    private val verifiedDeviceIds = MutableStateFlow<Set<DeviceId>>(emptySet())
    private val storedDevices = dataStore.data
        .catch { error ->
            if (error is IOException) {
                emit(emptyPreferences())
            } else {
                throw error
            }
        }
        .map { preferences -> decodeDevices(preferences[knownDevicesKey].orEmpty()) }
    override val devices: Flow<List<RemoteDevice>> = combine(storedDevices, verifiedDeviceIds) { devices, verifiedIds ->
        devices.map { device ->
            device.copy(
                availability = if (device.id in verifiedIds) DeviceAvailability.Online else DeviceAvailability.Offline,
            )
        }
    }

    override suspend fun save(resolvedDevice: ResolvedDevice) {
        dataStore.edit { preferences ->
            val devicesById = decodeDevices(preferences[knownDevicesKey].orEmpty()).associateByTo(linkedMapOf()) { it.id }
            devicesById[resolvedDevice.device.id] = resolvedDevice.device
            preferences[knownDevicesKey] = encodeDevices(devicesById.values)

            resolvedDevice.oneTimePassword?.takeIf(String::isNotBlank)?.let { password ->
                preferences[credentialKey(resolvedDevice.device.id)] = credentialCipher.encrypt(password)
            }
        }
        verifiedDeviceIds.update { verifiedIds -> verifiedIds + resolvedDevice.device.id }
    }

    override suspend fun remove(deviceId: DeviceId) {
        dataStore.edit { preferences ->
            val remaining = decodeDevices(preferences[knownDevicesKey].orEmpty()).filterNot { it.id == deviceId }
            preferences[knownDevicesKey] = encodeDevices(remaining)
            preferences.remove(credentialKey(deviceId))
        }
        verifiedDeviceIds.update { verifiedIds -> verifiedIds - deviceId }
    }

    override suspend fun credential(deviceId: DeviceId): String? {
        val encryptedCredential = dataStore.data.first()[credentialKey(deviceId)]
        return encryptedCredential?.let(credentialCipher::decrypt)
    }

    companion object {
        private val knownDevicesKey = stringPreferencesKey("known_devices_v1")

        fun create(context: Context, applicationScope: CoroutineScope): DeviceDataStore {
            val applicationContext = context.applicationContext
            val store = androidx.datastore.preferences.core.PreferenceDataStoreFactory.create(
                scope = applicationScope,
                produceFile = { applicationContext.preferencesDataStoreFile("pixels_devices") },
            )
            return DeviceDataStore(store, DeviceCredentialCipher())
        }

        private fun credentialKey(deviceId: DeviceId): Preferences.Key<String> {
            val digest = MessageDigest.getInstance("SHA-256").digest(deviceId.value.toByteArray(Charsets.UTF_8))
            val suffix = digest.joinToString(separator = "") { byte -> "%02x".format(byte) }
            return stringPreferencesKey("credential_$suffix")
        }

        private fun encodeDevices(devices: Collection<RemoteDevice>): String {
            val array = JSONArray()
            devices.forEach { device ->
                array.put(
                    JSONObject()
                        .put("id", device.id.value)
                        .put("name", device.displayName)
                        .put("platform", device.platformName)
                        .put("host", device.endpoint.host)
                        .put("panelPort", device.endpoint.panelPort)
                        .put("renderPort", device.endpoint.renderPort)
                        .put("lastConnected", device.lastConnectedEpochMillis ?: JSONObject.NULL)
                        .put("lastSeen", device.lastSeenEpochMillis ?: JSONObject.NULL),
                )
            }
            return array.toString()
        }

        private fun decodeDevices(serialized: String): List<RemoteDevice> {
            if (serialized.isBlank()) return emptyList()
            return runCatching {
                val array = JSONArray(serialized)
                buildList {
                    repeat(array.length()) { index ->
                        val item = array.getJSONObject(index)
                        add(
                            RemoteDevice(
                                id = DeviceId(item.getString("id")),
                                displayName = item.getString("name"),
                                platformName = item.optString("platform", "Windows"),
                                availability = DeviceAvailability.Offline,
                                endpoint = DeviceEndpoint(
                                    host = item.getString("host"),
                                    panelPort = item.getInt("panelPort"),
                                    renderPort = item.getInt("renderPort"),
                                ),
                                lastConnectedEpochMillis = item.optionalLong("lastConnected"),
                                lastSeenEpochMillis = item.optionalLong("lastSeen"),
                            ),
                        )
                    }
                }
            }.getOrDefault(emptyList())
        }

        private fun JSONObject.optionalLong(name: String): Long? = if (isNull(name)) null else getLong(name)
    }
}

private class DeviceCredentialCipher {
    fun encrypt(plainText: String): String {
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.ENCRYPT_MODE, getOrCreateKey())
        val encrypted = cipher.doFinal(plainText.toByteArray(Charsets.UTF_8))
        val encoder = Base64.getEncoder()
        return "${encoder.encodeToString(cipher.iv)}:${encoder.encodeToString(encrypted)}"
    }

    fun decrypt(encoded: String): String? = runCatching {
        val pieces = encoded.split(':', limit = 2)
        require(pieces.size == 2)
        val decoder = Base64.getDecoder()
        val cipher = Cipher.getInstance(TRANSFORMATION)
        cipher.init(Cipher.DECRYPT_MODE, getOrCreateKey(), GCMParameterSpec(128, decoder.decode(pieces[0])))
        cipher.doFinal(decoder.decode(pieces[1])).toString(Charsets.UTF_8)
    }.getOrNull()

    private fun getOrCreateKey(): SecretKey {
        val keyStore = KeyStore.getInstance(ANDROID_KEY_STORE).apply { load(null) }
        val existingKey = (keyStore.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.secretKey
        if (existingKey != null) return existingKey

        return KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, ANDROID_KEY_STORE).run {
            init(
                KeyGenParameterSpec.Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                )
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .build(),
            )
            generateKey()
        }
    }

    companion object {
        private const val ANDROID_KEY_STORE = "AndroidKeyStore"
        private const val KEY_ALIAS = "pixels_device_credentials_v1"
        private const val TRANSFORMATION = "AES/GCM/NoPadding"
    }
}
