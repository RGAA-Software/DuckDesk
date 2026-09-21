package yun.pixels.client.core.domain.update

class AndroidTufTrustedRoot(
    val distribution: String,
    val releaseNamespace: String,
    val oemId: String?,
    val version: Long,
    rootBytes: ByteArray,
) {
    private val trustedRootBytes = rootBytes.copyOf()

    fun copyRootBytes(): ByteArray = trustedRootBytes.copyOf()
}

sealed interface AndroidTufTrustedRootState {
    data object Empty : AndroidTufTrustedRootState

    data class Present(val trustedRoot: AndroidTufTrustedRoot) : AndroidTufTrustedRootState

    data object Invalid : AndroidTufTrustedRootState
}

interface AndroidTufTrustedRootStore {
    fun load(): AndroidTufTrustedRootState

    fun save(trustedRoot: AndroidTufTrustedRoot): Boolean
}
