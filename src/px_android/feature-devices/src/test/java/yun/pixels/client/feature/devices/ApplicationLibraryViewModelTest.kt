package yun.pixels.client.feature.devices

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
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

        viewModel.start(viewModel.state.value.applications.single())
        advanceUntilIdle()

        assertEquals(RemoteApplicationInstance.State.Running, viewModel.state.value.applications.single().runningInstance?.state)
        assertEquals(null, viewModel.state.value.pendingAppId)
    }
}

private class FakeApplicationRepository : ApplicationRepository {
    private var running: RemoteApplicationInstance? = null

    override suspend fun applications(): AccountResult<List<RemoteApplication>> = AccountResult.Success(
        listOf(RemoteApplication("app-1", "Editor", "", running)),
    )

    override suspend fun start(appId: String, clientNonce: String): AccountResult<RemoteApplicationInstance> {
        val instance = RemoteApplicationInstance("instance-1", RemoteApplicationInstance.State.Running, true)
        running = instance
        return AccountResult.Success(instance)
    }

    override suspend fun stop(instanceId: String): AccountResult<Unit> {
        running = null
        return AccountResult.Success(Unit)
    }
}
