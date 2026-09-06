package yun.pixels.client.feature.devices

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.async
import kotlinx.coroutines.CoroutineStart
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
import yun.pixels.client.core.domain.device.DeviceDiscovery
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.DeviceResolution
import yun.pixels.client.core.domain.device.DeviceResolutionFailure
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.domain.device.RemoteDevice
import yun.pixels.client.core.domain.device.ResolvedDevice
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.JoinMode
import yun.pixels.client.core.domain.session.RemoteSessionTarget

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
        val viewModel = DeviceHomeViewModel(
            directory,
            FakeDeviceResolver(DeviceResolution.Success(resolved)),
            FakeDeviceDiscovery(),
            FakeAccountRepository(),
        )

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
        val viewModel = DeviceHomeViewModel(directory, FakeDeviceResolver(resolution), FakeDeviceDiscovery(), FakeAccountRepository())

        viewModel.onAction(DeviceHomeAction.ConnectionInputChanged("192.168.1.99"))
        viewModel.onAction(DeviceHomeAction.Connect)
        advanceUntilIdle()

        assertNull(directory.saved)
        assertEquals("192.168.1.99", viewModel.uiState.value.connectionInput)
        assertEquals(ConnectionInputError.Unreachable, viewModel.uiState.value.inputError)
    }

    @Test
    fun signingInLoadsAccountDevices() = runTest(dispatcher) {
        val account = FakeAccountRepository()
        val viewModel = DeviceHomeViewModel(
            FakeDeviceDirectory(),
            FakeDeviceResolver(DeviceResolution.Failure(DeviceResolutionFailure.Unreachable)),
            FakeDeviceDiscovery(),
            account,
        )
        account.signIn()

        advanceUntilIdle()

        assertEquals(account.availableDevices, viewModel.uiState.value.accountDevices)
        assertEquals(AccountSummary.SignedIn::class, viewModel.uiState.value.account::class)
        assertNull(viewModel.uiState.value.accountFailure)
    }

    @Test
    fun accountRefreshFailureIsExposedWithoutInventingDevices() = runTest(dispatcher) {
        val account = FakeAccountRepository(devicesResult = AccountResult.Failure(AccountFailure.NetworkUnavailable))
        account.signIn()
        val viewModel = DeviceHomeViewModel(
            FakeDeviceDirectory(),
            FakeDeviceResolver(DeviceResolution.Failure(DeviceResolutionFailure.Unreachable)),
            FakeDeviceDiscovery(),
            account,
        )

        advanceUntilIdle()

        assertEquals(emptyList<AccountDevice>(), viewModel.uiState.value.accountDevices)
        assertEquals(AccountFailure.NetworkUnavailable, viewModel.uiState.value.accountFailure)
    }

    @Test
    fun discoveryPersistsEveryVerifiedDevice() = runTest(dispatcher) {
        val directory = FakeDeviceDirectory()
        val discovered = listOf(resolvedDevice(), resolvedDevice().copy(device = resolvedDevice().device.copy(id = DeviceId("desktop-2"))))
        val viewModel = DeviceHomeViewModel(
            directory,
            FakeDeviceResolver(DeviceResolution.Failure(DeviceResolutionFailure.Unreachable)),
            FakeDeviceDiscovery(discovered),
            FakeAccountRepository(),
        )

        viewModel.onAction(DeviceHomeAction.DiscoverLocal)
        advanceUntilIdle()

        assertEquals(discovered, directory.savedDevices)
        assertEquals(false, viewModel.uiState.value.isDiscovering)
    }

    @Test
    fun directRemoteRequestUsesEncryptedDirectoryCredential() = runTest(dispatcher) {
        val device = resolvedDevice().device
        val directory = FakeDeviceDirectory(credentialValue = "one-time-password")
        val viewModel = DeviceHomeViewModel(
            directory,
            FakeDeviceResolver(DeviceResolution.Failure(DeviceResolutionFailure.Unreachable)),
            FakeDeviceDiscovery(),
            FakeAccountRepository(),
        )
        val request = async(start = CoroutineStart.UNDISPATCHED) { viewModel.remoteRequests.first() }

        viewModel.onAction(DeviceHomeAction.StartRemoteDesktop(device))
        advanceUntilIdle()

        val target = request.await().target as RemoteSessionTarget.Direct
        assertEquals(device, target.device)
        assertEquals("one-time-password", target.credential)
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

private class FakeDeviceDirectory(private val credentialValue: String? = null) : DeviceDirectory {
    override val devices: Flow<List<RemoteDevice>> = MutableStateFlow(emptyList())
    var saved: ResolvedDevice? = null
    val savedDevices = mutableListOf<ResolvedDevice>()

    override suspend fun save(resolvedDevice: ResolvedDevice) {
        saved = resolvedDevice
        savedDevices += resolvedDevice
    }

    override suspend fun remove(deviceId: DeviceId) = Unit

    override suspend fun credential(deviceId: DeviceId): String? = credentialValue
}

private class FakeDeviceDiscovery(private val devices: List<ResolvedDevice> = emptyList()) : DeviceDiscovery {
    override suspend fun discover(): List<ResolvedDevice> = devices
}

private class FakeDeviceResolver(private val resolution: DeviceResolution) : DeviceResolver {
    override suspend fun resolve(connectionInput: String): DeviceResolution = resolution
}

private class FakeAccountRepository(
    private val devicesResult: AccountResult<List<AccountDevice>>? = null,
) : AccountRepository {
    private val mutableState = MutableStateFlow<AccountState>(AccountState.SignedOut)
    override val state: StateFlow<AccountState> = mutableState
    val availableDevices = listOf(AccountDevice("account-device", "Office PC", true, 1_000L))

    fun signIn() {
        mutableState.value = AccountState.SignedIn(
            AccountSession(
                endpoint = ConsoleEndpoint("https://console.example.com"),
                profile = AccountProfile("user-1", "alice", null, false),
                accessToken = "token",
                expiresAtEpochMillis = Long.MAX_VALUE,
                absoluteExpiresAtEpochMillis = Long.MAX_VALUE,
            ),
        )
    }

    override suspend fun restore() = Unit

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> =
        AccountResult.Failure(AccountFailure.InvalidCredentials)

    override suspend fun logout(): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun devices(): AccountResult<List<AccountDevice>> = devicesResult ?: AccountResult.Success(availableDevices)

    override suspend fun issueTicket(
        deviceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = AccountResult.Failure(AccountFailure.DeviceOffline)
}
