package yun.pixels.client.feature.devices

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.TestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.DeviceResolution
import yun.pixels.client.core.domain.device.DeviceResolutionFailure
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.domain.device.RemoteDevice
import yun.pixels.client.core.domain.device.ResolvedDevice

@OptIn(ExperimentalCoroutinesApi::class)
class DeviceHomeViewModelTest {
    private val dispatcher: TestDispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun verifiedDeviceIsSavedAndEditorIsCleared() = runTest(dispatcher) {
        val resolved = resolvedDevice()
        val directory = FakeDeviceDirectory()
        val viewModel = DeviceHomeViewModel(directory, FakeDeviceResolver(DeviceResolution.Success(resolved)))

        viewModel.onAction(DeviceHomeAction.ConnectionInputChanged("192.168.1.8"))
        viewModel.onAction(DeviceHomeAction.Connect)
        advanceUntilIdle()

        assertEquals(resolved, directory.saved)
        assertEquals("", viewModel.uiState.value.connectionInput)
        assertNull(viewModel.uiState.value.inputError)
    }

    @Test
    fun unreachableDeviceRemainsInEditorWithTypedError() = runTest(dispatcher) {
        val directory = FakeDeviceDirectory()
        val resolution = DeviceResolution.Failure(DeviceResolutionFailure.Unreachable)
        val viewModel = DeviceHomeViewModel(directory, FakeDeviceResolver(resolution))

        viewModel.onAction(DeviceHomeAction.ConnectionInputChanged("192.168.1.99"))
        viewModel.onAction(DeviceHomeAction.Connect)
        advanceUntilIdle()

        assertNull(directory.saved)
        assertEquals("192.168.1.99", viewModel.uiState.value.connectionInput)
        assertEquals(ConnectionInputError.Unreachable, viewModel.uiState.value.inputError)
    }

    private fun resolvedDevice(): ResolvedDevice = ResolvedDevice(
        device = RemoteDevice(
            id = DeviceId("desktop-1"),
            displayName = "Studio",
            platformName = "Windows",
            availability = DeviceAvailability.Online,
            endpoint = DeviceEndpoint("192.168.1.8"),
        ),
        oneTimePassword = "123456",
    )
}

private class FakeDeviceDirectory : DeviceDirectory {
    override val devices: Flow<List<RemoteDevice>> = MutableStateFlow(emptyList())
    var saved: ResolvedDevice? = null

    override suspend fun save(resolvedDevice: ResolvedDevice) {
        saved = resolvedDevice
    }

    override suspend fun remove(deviceId: DeviceId) = Unit

    override suspend fun credential(deviceId: DeviceId): String? = null
}

private class FakeDeviceResolver(private val resolution: DeviceResolution) : DeviceResolver {
    override suspend fun resolve(connectionInput: String): DeviceResolution = resolution
}
