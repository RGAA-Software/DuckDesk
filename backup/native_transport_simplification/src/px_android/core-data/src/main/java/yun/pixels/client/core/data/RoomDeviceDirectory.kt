package yun.pixels.client.core.data

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.update
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.RemoteDevice
import yun.pixels.client.core.domain.device.ResolvedDevice

class RoomDeviceDirectory(
    private val deviceDao: DeviceDao,
    private val credentialCipher: DeviceCredentialCipher = AndroidKeystoreDeviceCredentialCipher(),
) : DeviceDirectory {
    private val verifiedDeviceIds = MutableStateFlow<Set<DeviceId>>(emptySet())

    override val devices: Flow<List<RemoteDevice>> = combine(deviceDao.observeAll(), verifiedDeviceIds) { stored, verifiedIds ->
        stored.map { device ->
            device.toDomain(
                availability = if (DeviceId(device.deviceId) in verifiedIds) {
                    DeviceAvailability.Online
                } else {
                    DeviceAvailability.Offline
                },
            )
        }
    }

    override suspend fun save(resolvedDevice: ResolvedDevice) {
        val existingCredential = deviceDao.encryptedCredential(resolvedDevice.device.id.value)
        val encryptedCredential = resolvedDevice.oneTimePassword
            ?.takeIf(String::isNotBlank)
            ?.let(credentialCipher::encrypt)
            ?: existingCredential
        deviceDao.upsert(resolvedDevice.device.toStored(encryptedCredential))
        verifiedDeviceIds.update { verifiedIds -> verifiedIds + resolvedDevice.device.id }
    }

    override suspend fun remove(deviceId: DeviceId) {
        deviceDao.delete(deviceId.value)
        verifiedDeviceIds.update { verifiedIds -> verifiedIds - deviceId }
    }

    override suspend fun credential(deviceId: DeviceId): String? = deviceDao.encryptedCredential(deviceId.value)?.let(credentialCipher::decrypt)
}

interface DeviceCredentialCipher {
    fun encrypt(plainText: String): String

    fun decrypt(encoded: String): String?
}

private class AndroidKeystoreDeviceCredentialCipher : DeviceCredentialCipher {
    private val cipher = AndroidKeystoreTextCipher(KEY_ALIAS)

    override fun encrypt(plainText: String): String = cipher.encrypt(plainText)

    override fun decrypt(encoded: String): String? = cipher.decrypt(encoded)

    companion object {
        private const val KEY_ALIAS = "pixels_device_credentials_v1"
    }
}

internal fun StoredDevice.toDomain(availability: DeviceAvailability): RemoteDevice = RemoteDevice(
    id = DeviceId(deviceId),
    displayName = displayName,
    platformName = platformName,
    availability = availability,
    endpoint = DeviceEndpoint(host, panelPort, renderPort),
    lastConnectedEpochMillis = lastConnectedEpochMillis,
    lastSeenEpochMillis = lastSeenEpochMillis,
)

private fun RemoteDevice.toStored(encryptedCredential: String?): StoredDevice = StoredDevice(
    deviceId = id.value,
    displayName = displayName,
    platformName = platformName,
    host = endpoint.host,
    panelPort = endpoint.panelPort,
    renderPort = endpoint.renderPort,
    encryptedCredential = encryptedCredential,
    lastConnectedEpochMillis = lastConnectedEpochMillis,
    lastSeenEpochMillis = lastSeenEpochMillis,
)
