package yun.pixels.client

import android.app.Application
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
import yun.pixels.client.core.data.SharedPreferencesDeploymentIdentityWatermarkStore
import yun.pixels.client.core.data.createDeviceDirectory
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceDiscovery
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.network.ConsoleApiClient
import yun.pixels.client.core.network.ConsoleApplicationRepository
import yun.pixels.client.core.network.ConsoleSessionCoordinator
import yun.pixels.client.core.network.ConsoleResourceConnectionRenewer
import yun.pixels.client.core.network.DeploymentIdentityConfiguration

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
    private val deploymentIdentity = requireNotNull(
        DeploymentIdentityConfiguration.create(
            canonicalTrustStore = Base64.getDecoder().decode(BuildConfig.DEPLOYMENT_TRUST_STORE_BASE64),
            expectedKind = if (BuildConfig.DEPLOYMENT_DISTRIBUTION == "customer") "private" else "official",
            expectedDeploymentId = BuildConfig.EXPECTED_DEPLOYMENT_ID.ifEmpty { null },
            minimumCertificateVersion = BuildConfig.MINIMUM_DEPLOYMENT_CERTIFICATE_VERSION,
            minimumDescriptorRevision = BuildConfig.MINIMUM_DESCRIPTOR_REVISION,
            minimumTrustEpoch = BuildConfig.DEPLOYMENT_TRUST_EPOCH,
            clientBuild = BuildConfig.VERSION_CODE.toLong(),
            protocolVersion = 1,
        ),
    ) { "Pixels deployment identity configuration is invalid" }
    private val consoleApi = ConsoleApiClient(
        deploymentIdentity,
        SharedPreferencesDeploymentIdentityWatermarkStore.create(application),
    )

    val deviceDirectory: DeviceDirectory = createDeviceDirectory(application)
    val deviceResolver: DeviceResolver = PanelDeviceResolver()
    val deviceDiscovery: DeviceDiscovery = AndroidLanDeviceDiscovery(application)
    val installationIdentity = DataStoreInstallationIdentity.create(application, applicationScope)
    val remoteSessionPreferences = DataStoreRemoteSessionPreferencesRepository.create(application, applicationScope)
    val consoleSessionRepository = ConsoleSessionCoordinator(
        api = consoleApi,
        endpointStore = DataStoreConsoleEndpointStore.create(application, applicationScope),
        sessionStore = AndroidConsoleSessionStore.create(application, applicationScope),
        fixedEndpoint = BuildConfig.OFFICIAL_CONSOLE_URL.ifEmpty { null },
    ).also { repository -> applicationScope.launch { repository.restore() } }
    val accountRepository: AccountRepository = consoleSessionRepository
    val applicationRepository: ApplicationRepository = ConsoleApplicationRepository(consoleApi, consoleSessionRepository)
    val resourceConnectionRenewer = ConsoleResourceConnectionRenewer(consoleApi, consoleApi, consoleSessionRepository)
}
