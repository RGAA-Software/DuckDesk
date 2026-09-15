package yun.pixels.client.feature.devices

import kotlinx.coroutines.async
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Before
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.AccountConnection
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationInstance

@OptIn(ExperimentalCoroutinesApi::class)
class ApplicationLibraryViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() = Dispatchers.setMain(dispatcher)

    @After
    fun tearDown() = Dispatchers.resetMain()

    @Test
    fun refreshAndStartPublishServerState() = runTest(dispatcher) {
        val repository = FakeApplicationRepository()
        val viewModel = ApplicationLibraryViewModel(repository)

        viewModel.refresh()
        advanceUntilIdle()
        assertEquals(null, viewModel.state.value.applications.single().runningInstance)

        val remoteRequest = async { viewModel.remoteRequests.first() }
        viewModel.start(viewModel.state.value.applications.single())
        advanceUntilIdle()

        assertEquals(RemoteApplicationInstance.State.Running, viewModel.state.value.applications.single().runningInstance?.state)
        assertEquals(null, viewModel.state.value.pendingAppId)
        assertEquals("Editor", remoteRequest.await().target.displayName)
    }

    @Test
    fun runningInstanceCanReconnectWithoutStartingAnotherInstance() = runTest(dispatcher) {
        val repository = FakeApplicationRepository(runningInitially = true)
        val viewModel = ApplicationLibraryViewModel(repository)
        viewModel.refresh()
        advanceUntilIdle()

        val remoteRequest = async { viewModel.remoteRequests.first() }
        viewModel.connect(viewModel.state.value.applications.single())
        advanceUntilIdle()

        assert(remoteRequest.await().id.value.isNotBlank())
        assertEquals(0, repository.startCount)
    }
}

private class FakeApplicationRepository(runningInitially: Boolean = false) : ApplicationRepository {
    private var running: RemoteApplicationInstance? = if (runningInitially) runningInstance() else null
    var startCount: Int = 0
        private set

    override suspend fun applications(): AccountResult<List<RemoteApplication>> = AccountResult.Success(
        listOf(RemoteApplication("app-1", "Editor", "", running)),
    )

    override suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance> {
        startCount += 1
        val instance = runningInstance()
        running = instance
        return AccountResult.Success(instance)
    }

    override suspend fun stop(instanceId: String): AccountResult<Unit> {
        running = null
        return AccountResult.Success(Unit)
    }

    override suspend fun resolveConnection(instanceId: String): AccountResult<AccountConnection> = AccountResult.Success(
        AccountConnection(
            host = "192.168.1.2",
            port = 4601,
            deviceId = "device-1",
            instanceId = instanceId,
            passwordHash = "password-hash",
            relayHost = "",
            relayPort = 0,
            signalDeviceId = "server_device-1__instance__instance-1",
        ),
    )

    private fun runningInstance() = RemoteApplicationInstance("instance-1", RemoteApplicationInstance.State.Running, true)
}
