package yun.pixels.client.core.network

import java.io.ByteArrayOutputStream
import java.net.URI
import javax.net.ssl.HttpsURLConnection
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.update.AndroidUpdateRepository
import yun.pixels.client.core.domain.update.AndroidUpdatePreparationRepository
import yun.pixels.client.core.domain.update.AndroidUpdateRelease
import yun.pixels.client.core.domain.update.PreparedAndroidUpdateVerifier

enum class AndroidTufRefreshResult {
    Success,
    InvalidRepositoryLocation,
    NetworkFailure,
    TrustRejected,
}

class TufVerifiedAndroidUpdateRepository(
    private val catalogRepository: AndroidUpdateRepository,
    private val repositoryRefresher: AndroidTufRepositoryRefresher,
    private val apkDownloader: AndroidApkDownloader,
    private val preparedUpdateVerifier: PreparedAndroidUpdateVerifier,
) : AndroidUpdatePreparationRepository {
    private val workflowMutex = Mutex()
    private var approvedRelease: AndroidUpdateRelease? = null

    override suspend fun latest(): AccountResult<AndroidUpdateRelease> = workflowMutex.withLock {
        approvedRelease = null
        val catalogResult = catalogRepository.latest()
        if (catalogResult !is AccountResult.Success) return@withLock catalogResult
        when (repositoryRefresher.refresh(catalogResult.value)) {
            AndroidTufRefreshResult.Success -> {
                approvedRelease = catalogResult.value
                catalogResult
            }
            AndroidTufRefreshResult.NetworkFailure -> AccountResult.Failure(AccountFailure.NetworkUnavailable)
            AndroidTufRefreshResult.InvalidRepositoryLocation,
            AndroidTufRefreshResult.TrustRejected,
            -> AccountResult.Failure(AccountFailure.InvalidResponse)
        }
    }

    override suspend fun prepare(releaseId: String) = workflowMutex.withLock {
        val release = approvedRelease?.takeIf { candidate -> candidate.releaseId == releaseId }
            ?: return@withLock AccountResult.Failure(AccountFailure.InvalidResponse)
        val preparedUpdate = apkDownloader.download(release)
            ?: return@withLock AccountResult.Failure(AccountFailure.NetworkUnavailable)
        if (!preparedUpdateVerifier.verify(preparedUpdate)) {
            apkDownloader.discard(preparedUpdate)
            return@withLock AccountResult.Failure(AccountFailure.InvalidResponse)
        }
        AccountResult.Success(preparedUpdate)
    }
}

class AndroidTufRepositoryRefresher private constructor(
    private val trustedRootManager: AndroidTufTrustedRootManager,
    private val requestExecutor: TufMetadataRequestExecutor,
    private val ioDispatcher: CoroutineDispatcher,
    private val nowEpochSeconds: () -> Long,
) {
    constructor(
        trustedRootManager: AndroidTufTrustedRootManager,
        ioDispatcher: CoroutineDispatcher = Dispatchers.IO,
    ) : this(
        trustedRootManager,
        TufMetadataRequestExecutor(::executeBoundedHttpsGet),
        ioDispatcher,
        { System.currentTimeMillis() / 1_000 },
    )

    internal constructor(
        trustedRootManager: AndroidTufTrustedRootManager,
        requestExecutor: TufMetadataRequestExecutor,
        ioDispatcher: CoroutineDispatcher,
        nowEpochSeconds: () -> Long,
        @Suppress("UNUSED_PARAMETER") testMarker: Unit = Unit,
    ) : this(trustedRootManager, requestExecutor, ioDispatcher, nowEpochSeconds)

    suspend fun refresh(release: AndroidUpdateRelease): AndroidTufRefreshResult = withContext(ioDispatcher) {
        val verificationTime = nowEpochSeconds()
        val metadataBaseUri = parseMetadataBaseUri(release.artifact.metadataBaseUrl)
            ?: return@withContext AndroidTufRefreshResult.InvalidRepositoryLocation
        if (release.repositoryRootVersion < trustedRootManager.currentVersion) {
            return@withContext AndroidTufRefreshResult.TrustRejected
        }
        while (trustedRootManager.currentVersion < release.repositoryRootVersion) {
            val nextVersion = trustedRootManager.currentVersion + 1
            val nextRootUrl = resolveMetadataUrl(metadataBaseUri, "$nextVersion.root.json")
                ?: return@withContext AndroidTufRefreshResult.InvalidRepositoryLocation
            val nextRootBytes = requestExecutor.get(nextRootUrl, MAXIMUM_METADATA_BYTES)
                ?: return@withContext AndroidTufRefreshResult.NetworkFailure
            if (!trustedRootManager.acceptNextRoot(nextRootBytes, verificationTime)) {
                return@withContext AndroidTufRefreshResult.TrustRejected
            }
        }

        val timestampBytes = fetchMetadata(metadataBaseUri, "timestamp.json")
            ?: return@withContext AndroidTufRefreshResult.NetworkFailure
        val snapshotBytes = fetchMetadata(metadataBaseUri, "snapshot.json")
            ?: return@withContext AndroidTufRefreshResult.NetworkFailure
        val targetsBytes = fetchMetadata(metadataBaseUri, "targets.json")
            ?: return@withContext AndroidTufRefreshResult.NetworkFailure
        val metadataAccepted = trustedRootManager.verifyAndCommitMetadata(
            release,
            timestampBytes,
            snapshotBytes,
            targetsBytes,
            verificationTime,
        )
        if (!metadataAccepted) {
            return@withContext AndroidTufRefreshResult.TrustRejected
        }
        AndroidTufRefreshResult.Success
    }

    private fun fetchMetadata(metadataBaseUri: URI, fileName: String): ByteArray? {
        val metadataUrl = resolveMetadataUrl(metadataBaseUri, fileName) ?: return null
        return requestExecutor.get(metadataUrl, MAXIMUM_METADATA_BYTES)
    }
}

internal fun interface TufMetadataRequestExecutor {
    fun get(url: String, maximumBytes: Int): ByteArray?
}

private fun parseMetadataBaseUri(value: String): URI? = runCatching {
    val uri = URI(value)
    if (
        uri.scheme != "https" || uri.host.isNullOrEmpty() || uri.userInfo != null ||
        uri.query != null || uri.fragment != null ||
        !uri.path.endsWith('/') || uri.port != -1 && uri.port !in 1..65535
    ) {
        return null
    }
    uri
}.getOrNull()

private fun resolveMetadataUrl(baseUri: URI, fileName: String): String? = runCatching {
    val resolvedUri = baseUri.resolve(fileName)
    if (
        resolvedUri.scheme != baseUri.scheme || resolvedUri.host != baseUri.host || resolvedUri.port != baseUri.port ||
        resolvedUri.userInfo != null || resolvedUri.query != null || resolvedUri.fragment != null
    ) {
        return null
    }
    resolvedUri.toASCIIString()
}.getOrNull()

private fun executeBoundedHttpsGet(url: String, maximumBytes: Int): ByteArray? {
    if (maximumBytes !in 1..MAXIMUM_METADATA_BYTES) return null
    val connection = runCatching { URI(url).toURL().openConnection() as HttpsURLConnection }.getOrNull() ?: return null
    return try {
        connection.requestMethod = "GET"
        connection.connectTimeout = CONNECT_TIMEOUT_MILLIS
        connection.readTimeout = READ_TIMEOUT_MILLIS
        connection.instanceFollowRedirects = false
        connection.useCaches = false
        connection.setRequestProperty("Accept", "application/json")
        connection.setRequestProperty("Accept-Encoding", "identity")
        connection.setRequestProperty("User-Agent", "Pixels-Android/1")
        if (connection.responseCode != 200) return null
        val contentEncoding = connection.contentEncoding
        if (contentEncoding != null && !contentEncoding.equals("identity", ignoreCase = true)) return null
        val contentLength = connection.contentLengthLong
        if (contentLength > maximumBytes) return null
        val initialCapacity = if (contentLength in 1..maximumBytes.toLong()) contentLength.toInt() else 8192
        val output = ByteArrayOutputStream(initialCapacity)
        connection.inputStream.use { input ->
            val buffer = ByteArray(8192)
            while (true) {
                val bytesRead = input.read(buffer)
                if (bytesRead < 0) break
                if (output.size() + bytesRead > maximumBytes) return null
                output.write(buffer, 0, bytesRead)
            }
        }
        output.toByteArray().takeIf(ByteArray::isNotEmpty)
    } catch (_: Exception) {
        null
    } finally {
        connection.disconnect()
    }
}

private const val MAXIMUM_METADATA_BYTES = 1024 * 1024
private const val CONNECT_TIMEOUT_MILLIS = 5_000
private const val READ_TIMEOUT_MILLIS = 8_000
