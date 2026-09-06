package yun.pixels.client

import android.app.Application
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import yun.pixels.client.core.data.DeviceDataStore
import yun.pixels.client.core.data.PanelDeviceResolver
import yun.pixels.client.core.domain.device.DeviceDirectory
import yun.pixels.client.core.domain.device.DeviceResolver

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

    val deviceDirectory: DeviceDirectory = DeviceDataStore.create(application, applicationScope)
    val deviceResolver: DeviceResolver = PanelDeviceResolver()
}
