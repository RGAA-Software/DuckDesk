package yun.pixels.client.feature.settings

import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.test.StandardTestDispatcher
import kotlinx.coroutines.test.UnconfinedTestDispatcher
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
import yun.pixels.client.core.domain.account.AccountProfile
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.JoinMode

@OptIn(ExperimentalCoroutinesApi::class)
class SettingsViewModelTest {
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
    fun successfulLoginClearsPasswordAndShowsProfile() = runTest(dispatcher) {
        val repository = FakeAccountRepository()
        val viewModel = SettingsViewModel(repository)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        viewModel.onAction(SettingsAction.ConsoleEndpointChanged("https://console.example.com"))
        viewModel.onAction(SettingsAction.UsernameChanged("alice"))
        viewModel.onAction(SettingsAction.PasswordChanged("secret"))
        viewModel.onAction(SettingsAction.Login)

        advanceUntilIdle()

        assertEquals("alice", viewModel.uiState.value.profile?.username)
        assertEquals("", viewModel.uiState.value.password)
        assertNull(viewModel.uiState.value.failure)
    }

    @Test
    fun failedLoginClearsPasswordAndKeepsTypedFailure() = runTest(dispatcher) {
        val repository = FakeAccountRepository(AccountResult.Failure(AccountFailure.InvalidCredentials))
        val viewModel = SettingsViewModel(repository)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        viewModel.onAction(SettingsAction.ConsoleEndpointChanged("https://console.example.com"))
        viewModel.onAction(SettingsAction.UsernameChanged("alice"))
        viewModel.onAction(SettingsAction.PasswordChanged("wrong"))
        viewModel.onAction(SettingsAction.Login)

        advanceUntilIdle()

        assertEquals("", viewModel.uiState.value.password)
        assertEquals(AccountFailure.InvalidCredentials, viewModel.uiState.value.failure)
        assertNull(viewModel.uiState.value.profile)
    }
}

private class FakeAccountRepository(
    private val loginResult: AccountResult<AccountSession>? = null,
) : AccountRepository {
    private val mutableState = MutableStateFlow<AccountState>(AccountState.SignedOut)
    override val state: StateFlow<AccountState> = mutableState

    override suspend fun restore() = Unit

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> {
        val result = loginResult ?: AccountResult.Success(accountSession(endpoint, username))
        mutableState.value = when (result) {
            is AccountResult.Success -> AccountState.SignedIn(result.value)
            is AccountResult.Failure -> AccountState.SignedOut
        }
        return result
    }

    override suspend fun logout(): AccountResult<Unit> {
        mutableState.value = AccountState.SignedOut
        return AccountResult.Success(Unit)
    }

    override suspend fun devices(): AccountResult<List<AccountDevice>> = AccountResult.Success(emptyList())

    override suspend fun issueTicket(
        deviceId: String,
        clientNonce: String,
        joinMode: JoinMode,
    ): AccountResult<ConnectionTicket> = AccountResult.Failure(AccountFailure.DeviceOffline)

    override suspend fun renewTicket(
        ticket: ConnectionTicket,
        clientNonce: String,
    ): AccountResult<ConnectionTicket> = AccountResult.Failure(AccountFailure.AuthenticationRequired)

    private fun accountSession(endpoint: String, username: String) = AccountSession(
        endpoint = ConsoleEndpoint(endpoint),
        profile = AccountProfile("user-1", username, null, false),
        accessToken = "token",
        expiresAtEpochMillis = Long.MAX_VALUE,
        absoluteExpiresAtEpochMillis = Long.MAX_VALUE,
    )
}
