package yun.pixels.client.core.data

import android.content.Context
import android.net.ConnectivityManager
import android.net.LinkAddress
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import yun.pixels.client.core.domain.device.DeviceDiscovery
import yun.pixels.client.core.domain.device.DeviceResolution
import yun.pixels.client.core.domain.device.DeviceResolver
import yun.pixels.client.core.domain.device.ResolvedDevice
import java.net.Inet4Address

class AndroidLanDeviceDiscovery(
    context: Context,
    private val resolver: DeviceResolver = PanelDeviceResolver(connectTimeoutMillis = 450, readTimeoutMillis = 650),
) : DeviceDiscovery {
    private val connectivityManager = context.applicationContext.getSystemService(ConnectivityManager::class.java)

    override suspend fun discover(): List<ResolvedDevice> {
        val linkAddress = activeIpv4LinkAddress() ?: return emptyList()
        val localAddress = linkAddress.address as Inet4Address
        val candidates = subnetCandidates(localAddress.address, linkAddress.prefixLength)
        val concurrency = Semaphore(MAX_CONCURRENT_PROBES)
        return coroutineScope {
            candidates.map { host ->
                async {
                    concurrency.withPermit {
                        when (val result = resolver.resolve(host)) {
                            is DeviceResolution.Success -> result.resolvedDevice
                            is DeviceResolution.Failure -> null
                        }
                    }
                }
            }.awaitAll().filterNotNull().distinctBy { it.device.id }
        }
    }

    private fun activeIpv4LinkAddress(): LinkAddress? {
        return runCatching {
            val network = connectivityManager.activeNetwork ?: return null
            connectivityManager.getLinkProperties(network)?.linkAddresses?.firstOrNull { linkAddress ->
                linkAddress.address is Inet4Address && !linkAddress.address.isLoopbackAddress && linkAddress.prefixLength in 1..30
            }
        }.getOrNull()
    }

    companion object {
        private const val MAX_CONCURRENT_PROBES = 32
    }
}

internal fun subnetCandidates(localAddress: ByteArray, sourcePrefixLength: Int): List<String> {
    require(localAddress.size == 4)
    require(sourcePrefixLength in 1..30)
    val effectivePrefixLength = maxOf(sourcePrefixLength, 24)
    val address = localAddress.fold(0) { result, octet -> (result shl 8) or (octet.toInt() and 0xff) }
    val hostBits = 32 - effectivePrefixLength
    val hostMask = (1L shl hostBits) - 1L
    val network = address.toLong() and hostMask.inv() and 0xffff_ffffL
    val broadcast = network or hostMask
    return ((network + 1) until broadcast)
        .asSequence()
        .filter { candidate -> candidate != address.toLong().and(0xffff_ffffL) }
        .map(::ipv4Address)
        .toList()
}

private fun ipv4Address(address: Long): String = listOf(24, 16, 8, 0)
    .joinToString(".") { shift -> ((address shr shift) and 0xff).toString() }
