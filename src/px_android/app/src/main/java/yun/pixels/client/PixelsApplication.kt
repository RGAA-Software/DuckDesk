package yun.pixels.client

import android.app.Application
import java.io.File
import java.util.Base64
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import yun.pixels.client.core.data.AndroidConsoleSessionStore
import yun.pixels.client.core.data.AndroidLanDeviceDiscovery
import yun.pixels.client.core.data.DataStoreConsoleEndpointStore
import yun.pixels.client.core.data.DataStoreInstallationIdentity
import yun.pixels.client.core.data.DataStoreRemoteSessionPreferencesRepository
import yun.pixels.client.core.data.PanelDeviceResolver
import yun.pixels.client.core.data.SharedPreferencesAndroidTufTrustedRootStore
import yun.pixels.client.core.data.SharedPreferencesAndroidUpdateInstallationStore
import yun.pixels.client.core.data.createDeviceDirectory
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceDiscovery
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.domain.update.AndroidUpdatePreparationRepository
import yun.pixels.client.core.domain.update.AndroidUpdateInstaller
import yun.pixels.client.core.network.AndroidApkDownloader
import yun.pixels.client.core.network.AndroidReleaseIdentity
import yun.pixels.client.core.network.AndroidTufTrustConfiguration
import yun.pixels.client.core.network.AndroidTufTrustedRootManager
import yun.pixels.client.core.network.AndroidTufRepositoryRefresher
import yun.pixels.client.core.network.ConsoleApiClient
import yun.pixels.client.core.network.ConsoleAndroidUpdateRepository
import yun.pixels.client.core.network.ConsoleApplicationRepository
import yun.pixels.client.core.network.ConsoleResourceConnectionRenewer
import yun.pixels.client.core.network.ConsoleSessionCoordinator
import yun.pixels.client.core.network.TufVerifiedAndroidUpdateRepository
import yun.pixels.client.update.AndroidApkPlatformVerifier
import yun.pixels.client.update.AndroidPackageInstaller

class PixelsApplication : Application() {
    lateinit var graph: PixelsAppGraph
        private set

    override fun onCreate() {
        super.onCreate()
        graph = PixelsAppGraph(this)
    }
}

class PixelsAppGraph(application: Application) {
    private val applicationScope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val androidReleaseIdentity = requireNotNull(
        AndroidReleaseIdentity.create(
            distribution = BuildConfig.DEPLOYMENT_DISTRIBUTION,
            releaseNamespace = BuildConfig.RELEASE_NAMESPACE,
            oemId = BuildConfig.OEM_ID.ifEmpty { null },
        ),
    ) { "Pixels Android release identity configuration is invalid" }
    val androidTufTrustConfiguration = requireNotNull(
        AndroidTufTrustConfiguration.create(Base64.getDecoder().decode(BuildConfig.TUF_INITIAL_ROOT_BASE64)),
    ) { "Pixels Android TUF initial root is invalid or expired" }
    val androidTufTrustedRootManager = requireNotNull(
        AndroidTufTrustedRootManager.create(
            androidTufTrustConfiguration,
            androidReleaseIdentity,
            SharedPreferencesAndroidTufTrustedRootStore.create(application),
        ),
    ) { "Pixels Android TUF trusted root state is invalid" }
    private val consoleApi = ConsoleApiClient(androidReleaseIdentity)

    val deviceDirectory: DeviceDirectory = createDeviceDirectory(application)
    val deviceResolver: DeviceResolver = PanelDeviceResolver()
    val deviceDiscovery: DeviceDiscovery = AndroidLanDeviceDiscovery(application)
    val installationIdentity = DataStoreInstallationIdentity.create(application, applicationScope)
    val remoteSessionPreferences = DataStoreRemoteSessionPreferencesRepository.create(application, applicationScope)
    val consoleSessionRepository = ConsoleSessionCoordinator(
        api = consoleApi,
        endpointStore = DataStoreConsoleEndpointStore.create(application, applicationScope),
        sessionStore = AndroidConsoleSessionStore.create(application, applicationScope),
        fixedEndpoint = BuildConfig.OFFICIAL_CONSOLE_URL.takeIf { BuildConfig.DEPLOYMENT_DISTRIBUTION == "official" },
        forbiddenEndpoint = BuildConfig.OFFICIAL_CONSOLE_URL.takeIf { BuildConfig.DEPLOYMENT_DISTRIBUTION == "customer" },
    ).also { repository -> applicationScope.launch { repository.restore() } }
    val accountRepository: AccountRepository = consoleSessionRepository
    val applicationRepository: ApplicationRepository = ConsoleApplicationRepository(consoleApi, consoleSessionRepository)
    private val apkPlatformVerifier = AndroidApkPlatformVerifier(
        packageManager = application.packageManager,
        expectedPackageName = application.packageName,
        installedVersionCode = BuildConfig.VERSION_CODE.toLong(),
    )
    val updateRepository: AndroidUpdatePreparationRepository = TufVerifiedAndroidUpdateRepository(
        ConsoleAndroidUpdateRepository(consoleApi, consoleSessionRepository),
        AndroidTufRepositoryRefresher(androidTufTrustedRootManager),
        AndroidApkDownloader(File(application.filesDir, "updates/prepared")),
        apkPlatformVerifier,
    )
    val updateInstaller: AndroidUpdateInstaller = AndroidPackageInstaller(
        context = application,
        verifier = apkPlatformVerifier,
        installationStore = SharedPreferencesAndroidUpdateInstallationStore.create(application),
        currentBuildNumber = BuildConfig.VERSION_CODE.toLong(),
    )
    val resourceConnectionRenewer = ConsoleResourceConnectionRenewer(consoleApi, consoleApi, consoleSessionRepository)
}
