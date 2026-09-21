package yun.pixels.client.feature.cloudapps

import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.async
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.resetMain
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.setMain
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Before
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountDevice
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ConsoleSessionRepository
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationAccess
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.RemoteApplicationType
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.session.RemoteSessionTarget

@OptIn(ExperimentalCoroutinesApi::class)
class CloudAppsViewModelTest {
    private val dispatcher = StandardTestDispatcher()

    @Before
    fun setUp() {
        Dispatchers.setMain(dispatcher)
    }

    @After
    fun tearDown() {
        Dispatchers.resetMain()
    }

    @Test
    fun configuredEndpointLoadsCatalogOnce() = runTest(dispatcher) {
        val application = application()
        val repository = FakeApplicationRepository(listOf(application))
        val viewModel = CloudAppsViewModel(repository, FakeConsoleSessionRepository())

        advanceUntilIdle()

        assertEquals(1, repository.applicationCalls)
        assertEquals(listOf(application), viewModel.state.value.applications)
        assertEquals(true, viewModel.state.value.endpointConfigured)
        assertEquals(false, viewModel.state.value.signedIn)
    }

    @Test
    fun unsupportedApplicationCannotStart() = runTest(dispatcher) {
        val application = application(type = RemoteApplicationType.Rdp)
        val repository = FakeApplicationRepository(listOf(application))
        val viewModel = CloudAppsViewModel(repository, FakeConsoleSessionRepository())
        advanceUntilIdle()

        viewModel.start(application)
        advanceUntilIdle()

        assertEquals(emptyList<String>(), repository.startedAppIds)
        assertNull(viewModel.state.value.pendingAppId)
    }

    @Test
    fun reconnectEmitsDistinctCloudApplicationTarget() = runTest(dispatcher) {
        val instance = RemoteApplicationInstance("instance-1", "app-1", RemoteApplicationInstance.State.Running, true)
        val application = application(instance = instance)
        val connection = connection()
        val repository = FakeApplicationRepository(listOf(application), connectionResult = AccountResult.Success(connection))
        val viewModel = CloudAppsViewModel(repository, FakeConsoleSessionRepository())
        advanceUntilIdle()
        val request = async(start = CoroutineStart.UNDISPATCHED) { viewModel.remoteRequests.first() }

        viewModel.connect(application)
        advanceUntilIdle()

        val target = request.await().target as RemoteSessionTarget.CloudApplication
        assertEquals("Cloud Game", target.displayName)
        assertEquals("app-1", target.appId)
        assertEquals("instance-1", target.instanceId)
        assertEquals(connection, target.connection)
        assertEquals(listOf("instance-1"), repository.resolvedInstanceIds)
    }

    @Test
    fun startFailureClearsPendingStateAndExposesTypedFailure() = runTest(dispatcher) {
        val application = application()
        val repository = FakeApplicationRepository(
            initialApplications = listOf(application),
            startResult = AccountResult.Failure(AccountFailure.QuotaExceeded),
        )
        val viewModel = CloudAppsViewModel(repository, FakeConsoleSessionRepository())
        advanceUntilIdle()

        viewModel.start(application)
        advanceUntilIdle()

        assertEquals(AccountFailure.QuotaExceeded, viewModel.state.value.failure)
        assertNull(viewModel.state.value.pendingAppId)
    }

    @Test
    fun stopPollsUntilTheInstanceLeavesTheActiveSet() = runTest(dispatcher) {
        val instance = RemoteApplicationInstance("instance-1", "app-1", RemoteApplicationInstance.State.Running, true)
        val application = application(instance = instance)
        val repository = FakeApplicationRepository(listOf(application), stopAfterApplicationCalls = 3)
        val viewModel = CloudAppsViewModel(repository, FakeConsoleSessionRepository())
        advanceUntilIdle()

        viewModel.stop(application)
        advanceUntilIdle()

        assertEquals(3, repository.applicationCalls)
        assertNull(viewModel.state.value.applications.single().runningInstance)
        assertNull(viewModel.state.value.pendingAppId)
    }

    private fun application(
        type: RemoteApplicationType = RemoteApplicationType.GameHook,
        instance: RemoteApplicationInstance? = null,
    ) = RemoteApplication(
        appId = "app-1",
        name = "Cloud Game",
        coverUrl = "",
        type = type,
        access = RemoteApplicationAccess.Public,
        version = 1,
        runningInstance = instance,
    )

    private fun connection() = ResourceConnection(
        host = "render.example.com",
        port = 4613,
        remoteResourceId = "instance-1",
        sessionId = "session-1",
        sessionRevision = 2,
        frontendToken = "frontend-token",
        transport = "native",
        expiresAtEpochMillis = Long.MAX_VALUE,
    )
}

private class FakeApplicationRepository(
    private val initialApplications: List<RemoteApplication>,
    private val startResult: AccountResult<RemoteApplicationInstance> = AccountResult.Failure(AccountFailure.ServerError),
    private val connectionResult: AccountResult<ResourceConnection> = AccountResult.Failure(AccountFailure.DeviceOffline),
    private val stopAfterApplicationCalls: Int? = null,
) : ApplicationRepository {
    var applicationCalls = 0
    val startedAppIds = mutableListOf<String>()
    val resolvedInstanceIds = mutableListOf<String>()

    override suspend fun applications(): AccountResult<List<RemoteApplication>> {
        applicationCalls += 1
        val applications = if (stopAfterApplicationCalls != null && applicationCalls >= stopAfterApplicationCalls) {
            initialApplications.map { it.copy(runningInstance = null) }
        } else {
            initialApplications
        }
        return AccountResult.Success(applications)
    }

    override suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance> {
        startedAppIds += appId
        return startResult
    }

    override suspend fun stop(instanceId: String): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun resolveConnection(appId: String, instanceId: String): AccountResult<ResourceConnection> {
        resolvedInstanceIds += instanceId
        return connectionResult
    }
}

private class FakeConsoleSessionRepository(
    endpoint: ConsoleEndpoint? = ConsoleEndpoint("https://console.example.com"),
) : ConsoleSessionRepository {
    private val mutableState = MutableStateFlow<AccountState>(AccountState.SignedOut)
    private val mutableEndpoint = MutableStateFlow(endpoint)
    override val state: StateFlow<AccountState> = mutableState
    override val endpoint: StateFlow<ConsoleEndpoint?> = mutableEndpoint

    override suspend fun restore() = Unit

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> =
        AccountResult.Failure(AccountFailure.InvalidCredentials)

    override suspend fun logout(): AccountResult<Unit> = AccountResult.Success(Unit)

    override suspend fun devices(): AccountResult<List<AccountDevice>> = AccountResult.Success(emptyList())

    override suspend fun resolveConnection(deviceId: String): AccountResult<ResourceConnection> =
        AccountResult.Failure(AccountFailure.DeviceOffline)

    override suspend fun saveEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> {
        val value = ConsoleEndpoint(endpoint)
        mutableEndpoint.value = value
        return AccountResult.Success(value)
    }

    override suspend fun testEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> = AccountResult.Success(ConsoleEndpoint(endpoint))

    override suspend fun register(username: String, password: String): AccountResult<AccountSession> =
        AccountResult.Failure(AccountFailure.ServerError)
}
