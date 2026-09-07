package yun.pixels.client.core.data

import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.runTest
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.RemoteDevice
import yun.pixels.client.core.domain.device.ResolvedDevice

class RoomDeviceDirectoryTest {
    @Test
    fun saveEncryptsCredentialAndExposesVerifiedDeviceAsOnline() = runTest {
        val dao = FakeDeviceDao()
        val cipher = PrefixCipher()
        val directory = RoomDeviceDirectory(dao, cipher)
        val resolved = resolvedDevice(password = "secret")

        directory.save(resolved)

        assertEquals("encrypted:secret", dao.devices.value.single().encryptedCredential)
        assertEquals("secret", directory.credential(DeviceId("device-1")))
        assertEquals(DeviceAvailability.Online, directory.devices.first().single().availability)
    }

    @Test
    fun saveWithoutCredentialPreservesExistingCiphertext() = runTest {
        val dao = FakeDeviceDao()
        val directory = RoomDeviceDirectory(dao, PrefixCipher())
        directory.save(resolvedDevice(password = "secret"))

        directory.save(resolvedDevice(password = null))

        assertEquals("encrypted:secret", dao.devices.value.single().encryptedCredential)
    }

    @Test
    fun removeDeletesDeviceAndCredential() = runTest {
        val dao = FakeDeviceDao()
        val directory = RoomDeviceDirectory(dao, PrefixCipher())
        directory.save(resolvedDevice(password = "secret"))

        directory.remove(DeviceId("device-1"))

        assertEquals(emptyList<StoredDevice>(), directory.devices.first())
        assertNull(directory.credential(DeviceId("device-1")))
    }

    private fun resolvedDevice(password: String?) = ResolvedDevice(
        device = RemoteDevice(
            id = DeviceId("device-1"),
            displayName = "Office PC",
            platformName = "Windows",
            availability = DeviceAvailability.Online,
            endpoint = DeviceEndpoint("192.168.1.20"),
            lastSeenEpochMillis = 1234,
        ),
        oneTimePassword = password,
    )
}

private class PrefixCipher : DeviceCredentialCipher {
    override fun encrypt(plainText: String) = "encrypted:$plainText"

    override fun decrypt(encoded: String) = encoded.removePrefix("encrypted:")
}

private class FakeDeviceDao : DeviceDao {
    val devices = MutableStateFlow<List<StoredDevice>>(emptyList())

    override fun observeAll(): Flow<List<StoredDevice>> = devices

    override suspend fun encryptedCredential(deviceId: String): String? = devices.value
        .firstOrNull { it.deviceId == deviceId }
        ?.encryptedCredential

    override suspend fun upsert(device: StoredDevice) {
        devices.value = devices.value.filterNot { it.deviceId == device.deviceId } + device
    }

    override suspend fun delete(deviceId: String) {
        devices.value = devices.value.filterNot { it.deviceId == deviceId }
    }
}
