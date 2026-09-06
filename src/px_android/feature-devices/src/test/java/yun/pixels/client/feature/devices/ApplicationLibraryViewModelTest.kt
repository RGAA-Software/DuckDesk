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
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.JoinMode
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

        assertEquals("session-1", remoteRequest.await().id.value)
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

    override suspend fun issueTicket(
        instanceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = AccountResult.Success(
        ConnectionTicket(
            ticket = "ticket",
            renewalToken = "renewal",
            launchUrl = "http://192.168.1.2:20371/web_client/?deviceId=device-1",
            expiresAtEpochMillis = Long.MAX_VALUE,
            logicalSessionId = "session-1",
            streamId = "stream-1",
            joinMode = joinMode,
            permissions = setOf("view", "input", "audio"),
            rtcIceConfigJson = "",
            relayHost = "",
            relayPort = 0,
            signalDeviceId = "server_device-1__instance__instance-1",
        ),
    )

    private fun runningInstance() = RemoteApplicationInstance("instance-1", RemoteApplicationInstance.State.Running, true)
}
