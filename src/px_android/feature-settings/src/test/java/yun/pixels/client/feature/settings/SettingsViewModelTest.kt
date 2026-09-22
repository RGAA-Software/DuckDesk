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
import yun.pixels.client.core.domain.account.ConsoleSessionRepository
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.AccountSession
import yun.pixels.client.core.domain.account.AccountState
import yun.pixels.client.core.domain.account.ConsoleEndpoint
import yun.pixels.client.core.domain.account.ResourceConnection
import yun.pixels.client.core.domain.update.AndroidUpdateArtifact
import yun.pixels.client.core.domain.update.AndroidUpdateInstaller
import yun.pixels.client.core.domain.update.AndroidUpdatePreparationRepository
import yun.pixels.client.core.domain.update.AndroidUpdateRelease
import yun.pixels.client.core.domain.update.PreparedAndroidUpdate

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
        val viewModel = settingsViewModel(repository)
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
        val viewModel = settingsViewModel(repository)
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

    @Test
    fun restoredEndpointCanBeUsedForLoginWithoutEditingAddress() = runTest(dispatcher) {
        val repository = FakeAccountRepository(initialEndpoint = "https://console.example.com")
        val viewModel = settingsViewModel(repository)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        advanceUntilIdle()

        viewModel.onAction(SettingsAction.UsernameChanged("alice"))
        viewModel.onAction(SettingsAction.PasswordChanged("secret"))
        viewModel.onAction(SettingsAction.Login)
        advanceUntilIdle()

        assertEquals("https://console.example.com", repository.lastLoginEndpoint)
        assertEquals("alice", viewModel.uiState.value.profile?.username)
    }

    @Test
    fun changingEndpointWhileSignedInRequiresConfirmationAndSignsOut() = runTest(dispatcher) {
        val repository = FakeAccountRepository(initialEndpoint = "https://old.example.com")
        repository.signIn("https://old.example.com", "alice")
        val viewModel = settingsViewModel(repository)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        advanceUntilIdle()

        viewModel.onAction(SettingsAction.ConsoleEndpointChanged("https://new.example.com"))
        viewModel.onAction(SettingsAction.SaveEndpoint)
        advanceUntilIdle()

        assertEquals(true, viewModel.uiState.value.confirmEndpointChange)
        assertEquals("alice", viewModel.uiState.value.profile?.username)
        assertEquals(emptyList<String>(), repository.savedEndpoints)

        viewModel.onAction(SettingsAction.ConfirmEndpointChange)
        advanceUntilIdle()

        assertEquals(listOf("https://new.example.com"), repository.savedEndpoints)
        assertEquals("https://new.example.com", viewModel.uiState.value.consoleEndpoint)
        assertNull(viewModel.uiState.value.profile)
        assertEquals(false, viewModel.uiState.value.confirmEndpointChange)
    }

    @Test
    fun officialEndpointCannotBeEditedOrSaved() = runTest(dispatcher) {
        val repository = FakeAccountRepository(initialEndpoint = "https://official.example.com", endpointEditable = false)
        val viewModel = settingsViewModel(repository)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        advanceUntilIdle()

        viewModel.onAction(SettingsAction.ConsoleEndpointChanged("https://private.example.com"))
        viewModel.onAction(SettingsAction.SaveEndpoint)
        viewModel.onAction(SettingsAction.TestEndpoint)
        advanceUntilIdle()

        assertEquals(false, viewModel.uiState.value.endpointEditable)
        assertEquals("https://official.example.com", viewModel.uiState.value.consoleEndpoint)
        assertEquals(emptyList<String>(), repository.savedEndpoints)
        assertEquals(emptyList<String>(), repository.testedEndpoints)
    }

    @Test
    fun verifiedUpdateFlowsFromDiscoveryThroughInstallerSubmission() = runTest(dispatcher) {
        val accountRepository = FakeAccountRepository(initialEndpoint = "https://console.example.com")
        accountRepository.signIn("https://console.example.com", "alice")
        val updateRepository = FakeUpdateRepository()
        val updateInstaller = FakeUpdateInstaller()
        val viewModel = SettingsViewModel(accountRepository, updateRepository, updateInstaller)
        backgroundScope.launch(UnconfinedTestDispatcher(testScheduler)) { viewModel.uiState.collect {} }
        advanceUntilIdle()

        viewModel.onAction(SettingsAction.CheckUpdate)
        advanceUntilIdle()

        assertEquals(UpdateStatus.Available, viewModel.uiState.value.updateStatus)
        assertEquals("1.2.3", viewModel.uiState.value.updateVersion)
        assertEquals(TARGET_BUILD_NUMBER, viewModel.uiState.value.updateBuildNumber)

        viewModel.onAction(SettingsAction.InstallUpdate)
        advanceUntilIdle()

        assertEquals(UpdateStatus.Submitted, viewModel.uiState.value.updateStatus)
        assertEquals(APPROVED_RELEASE_ID, updateRepository.preparedReleaseId)
        assertEquals(APPROVED_RELEASE_ID, updateInstaller.installedUpdate?.release?.releaseId)
    }

    private fun settingsViewModel(repository: ConsoleSessionRepository): SettingsViewModel =
        SettingsViewModel(repository, FakeUpdateRepository(), FakeUpdateInstaller())
}

private class FakeAccountRepository(
    private val loginResult: AccountResult<AccountSession>? = null,
    initialEndpoint: String? = null,
    override val endpointEditable: Boolean = true,
) : ConsoleSessionRepository {
    private val mutableState = MutableStateFlow<AccountState>(AccountState.SignedOut)
    override val state: StateFlow<AccountState> = mutableState
    private val mutableEndpoint = MutableStateFlow(initialEndpoint?.let(::ConsoleEndpoint))
    override val endpoint: StateFlow<ConsoleEndpoint?> = mutableEndpoint
    var lastLoginEndpoint: String? = null
    val savedEndpoints = mutableListOf<String>()
    val testedEndpoints = mutableListOf<String>()

    fun signIn(endpoint: String, username: String) {
        mutableEndpoint.value = ConsoleEndpoint(endpoint)
        mutableState.value = AccountState.SignedIn(accountSession(endpoint, username))
    }

    override suspend fun restore() = Unit

    override suspend fun login(endpoint: String, username: String, password: String): AccountResult<AccountSession> {
        lastLoginEndpoint = endpoint
        mutableEndpoint.value = ConsoleEndpoint(endpoint)
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

    override suspend fun resolveConnection(deviceId: String): AccountResult<ResourceConnection> =
        AccountResult.Failure(AccountFailure.DeviceOffline)

    override suspend fun saveEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> {
        val value = ConsoleEndpoint(endpoint)
        if (mutableEndpoint.value != value) mutableState.value = AccountState.SignedOut
        mutableEndpoint.value = value
        savedEndpoints += endpoint
        return AccountResult.Success(value)
    }

    override suspend fun testEndpoint(endpoint: String): AccountResult<ConsoleEndpoint> {
        testedEndpoints += endpoint
        return AccountResult.Success(ConsoleEndpoint(endpoint))
    }

    override suspend fun register(username: String, password: String): AccountResult<AccountSession> =
        login(mutableEndpoint.value?.baseUrl.orEmpty(), username, password)

    private fun accountSession(endpoint: String, username: String) = AccountSession(
        endpoint = ConsoleEndpoint(endpoint),
        profile = AccountProfile("user-1", username, null, false),
        accessToken = "token",
        expiresAtEpochMillis = Long.MAX_VALUE,
    )
}

private class FakeUpdateRepository : AndroidUpdatePreparationRepository {
    private val release = updateRelease()
    var preparedReleaseId: String? = null

    override suspend fun latest(): AccountResult<AndroidUpdateRelease> = AccountResult.Success(release)

    override suspend fun prepare(releaseId: String): AccountResult<PreparedAndroidUpdate> {
        preparedReleaseId = releaseId
        return if (releaseId == release.releaseId) {
            AccountResult.Success(PreparedAndroidUpdate(release, "/private/prepared.apk"))
        } else {
            AccountResult.Failure(AccountFailure.InvalidResponse)
        }
    }
}

private class FakeUpdateInstaller : AndroidUpdateInstaller {
    var installedUpdate: PreparedAndroidUpdate? = null

    override suspend fun install(preparedUpdate: PreparedAndroidUpdate): AccountResult<Unit> {
        installedUpdate = preparedUpdate
        return AccountResult.Success(Unit)
    }
}

private fun updateRelease() = AndroidUpdateRelease(
    releaseId = APPROVED_RELEASE_ID,
    repositoryPublicationSha256 = "11".repeat(32),
    repositoryRootVersion = 1,
    revision = 1,
    createdAtEpochMillis = 1,
    updatedAtEpochMillis = 1,
    artifact = AndroidUpdateArtifact(
        buildNumber = TARGET_BUILD_NUMBER,
        version = "1.2.3",
        metadataBaseUrl = "https://updates.example/metadata/",
        targetsBaseUrl = "https://updates.example/targets/",
        targetName = "android/android/official/stable/aarch64/123/pixels.apk",
        sha256 = "22".repeat(32),
        platformSignerSha256 = "33".repeat(32),
        sizeBytes = 1024,
    ),
)

private const val APPROVED_RELEASE_ID = "11111111-1111-4111-8111-111111111111"
private const val TARGET_BUILD_NUMBER = 123L
