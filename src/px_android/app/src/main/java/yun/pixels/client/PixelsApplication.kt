package yun.pixels.client

import android.app.Application
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import yun.pixels.client.core.data.EncryptedAccountSessionStore
import yun.pixels.client.core.data.AndroidLanDeviceDiscovery
import yun.pixels.client.core.data.DataStoreInstallationIdentity
import yun.pixels.client.core.data.PanelDeviceResolver
import yun.pixels.client.core.data.createDeviceDirectory
import yun.pixels.client.core.domain.account.AccountRepository
import yun.pixels.client.core.domain.account.ApplicationRepository
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceDiscovery
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.network.ConsoleAccountRepository
import yun.pixels.client.core.network.ConsoleApiClient
import yun.pixels.client.core.network.ConsoleApplicationRepository

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
    private val consoleApi = ConsoleApiClient()

    val deviceDirectory: DeviceDirectory = createDeviceDirectory(application)
    val deviceResolver: DeviceResolver = PanelDeviceResolver()
    val deviceDiscovery: DeviceDiscovery = AndroidLanDeviceDiscovery(application)
    val installationIdentity = DataStoreInstallationIdentity.create(application, applicationScope)
    val accountRepository: AccountRepository = ConsoleAccountRepository(
        api = consoleApi,
        sessionStore = EncryptedAccountSessionStore.create(application, applicationScope),
    ).also { repository -> applicationScope.launch { repository.restore() } }
    val applicationRepository: ApplicationRepository = ConsoleApplicationRepository(consoleApi, accountRepository)
}
