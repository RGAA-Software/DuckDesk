package yun.pixels.client.core.domain.update

class AndroidTufTrustedRoot(
    val distribution: String,
    val releaseNamespace: String,
    val oemId: String?,
    val version: Long,
    rootBytes: ByteArray,
    val metadataWatermark: AndroidTufMetadataWatermark?,
) {
    private val trustedRootBytes = rootBytes.copyOf()

    fun copyRootBytes(): ByteArray = trustedRootBytes.copyOf()
}

data class AndroidTufMetadataWatermark(
    val timestampVersion: Long,
    val timestampSha256: String,
    val snapshotVersion: Long,
    val snapshotSha256: String,
    val targetsVersion: Long,
    val targetsSha256: String,
)

sealed interface AndroidTufTrustedRootState {
    data object Empty : AndroidTufTrustedRootState

    data class Present(val trustedRoot: AndroidTufTrustedRoot) : AndroidTufTrustedRootState

    data object Invalid : AndroidTufTrustedRootState
}

interface AndroidTufTrustedRootStore {
    fun load(): AndroidTufTrustedRootState

    fun save(trustedRoot: AndroidTufTrustedRoot): Boolean
}
