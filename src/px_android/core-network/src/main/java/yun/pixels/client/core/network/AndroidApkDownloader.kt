package yun.pixels.client.core.network

import java.io.File
import java.net.URI
import java.nio.channels.Channels
import java.nio.file.Files
import java.nio.file.LinkOption
import java.nio.file.StandardCopyOption
import java.nio.file.StandardOpenOption
import java.security.MessageDigest
import java.util.UUID
import javax.net.ssl.HttpsURLConnection
import kotlinx.coroutines.CoroutineDispatcher
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.update.AndroidUpdateRelease
import yun.pixels.client.core.domain.update.PreparedAndroidUpdate

class AndroidApkDownloader private constructor(
    private val stagingDirectory: File,
    private val requestExecutor: ApkRequestExecutor,
    private val ioDispatcher: CoroutineDispatcher,
) {
    constructor(stagingDirectory: File, ioDispatcher: CoroutineDispatcher = Dispatchers.IO) : this(
        stagingDirectory,
        ApkRequestExecutor(::executeApkRequest),
        ioDispatcher,
    )

    internal constructor(
        stagingDirectory: File,
        requestExecutor: ApkRequestExecutor,
        ioDispatcher: CoroutineDispatcher,
        @Suppress("UNUSED_PARAMETER") testMarker: Unit = Unit,
    ) : this(stagingDirectory, requestExecutor, ioDispatcher)

    suspend fun download(release: AndroidUpdateRelease): PreparedAndroidUpdate? = withContext(ioDispatcher) {
        val artifact = release.artifact
        if (artifact.sizeBytes !in 1..MAXIMUM_APK_BYTES) return@withContext null
        val targetUrl = resolveTargetUrl(artifact.targetsBaseUrl, artifact.targetName) ?: return@withContext null
        if (!preparePrivateDirectory()) return@withContext null
        val finalPath = stagingDirectory.toPath().resolve("${release.releaseId}-${artifact.buildNumber}.apk")
        val temporaryPath = stagingDirectory.toPath().resolve(".${release.releaseId}-${UUID.randomUUID()}.part")
        if (Files.isSymbolicLink(finalPath) || Files.exists(temporaryPath, LinkOption.NOFOLLOW_LINKS)) return@withContext null
        val digest = MessageDigest.getInstance("SHA-256")
        var receivedBytes = 0L
        val downloaded = runCatching {
            Files.newByteChannel(
                temporaryPath,
                setOf(StandardOpenOption.CREATE_NEW, StandardOpenOption.WRITE, LinkOption.NOFOLLOW_LINKS),
            ).use { outputChannel ->
                Channels.newOutputStream(outputChannel).use { output ->
                    requestExecutor.download(targetUrl, artifact.sizeBytes) chunk@{ payloadBytes, byteCount ->
                        if (byteCount <= 0 || receivedBytes + byteCount > artifact.sizeBytes) return@chunk false
                        output.write(payloadBytes, 0, byteCount)
                        digest.update(payloadBytes, 0, byteCount)
                        receivedBytes += byteCount
                        true
                    }
                }
            }
            receivedBytes == artifact.sizeBytes && digest.digest().toHex() == artifact.sha256
        }.getOrDefault(false)
        if (!downloaded) {
            Files.deleteIfExists(temporaryPath)
            return@withContext null
        }
        val committed = runCatching {
            Files.move(temporaryPath, finalPath, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING)
            true
        }.getOrDefault(false)
        if (!committed) {
            Files.deleteIfExists(temporaryPath)
            return@withContext null
        }
        PreparedAndroidUpdate(release, finalPath.toAbsolutePath().normalize().toString())
    }

    suspend fun discard(preparedUpdate: PreparedAndroidUpdate): Boolean = withContext(ioDispatcher) {
        val stagedPath = runCatching { File(preparedUpdate.stagedApkPath).toPath().toAbsolutePath().normalize() }.getOrNull()
            ?: return@withContext false
        val privateDirectoryPath = stagingDirectory.toPath().toAbsolutePath().normalize()
        if (stagedPath.parent != privateDirectoryPath || !stagedPath.fileName.toString().endsWith(".apk")) return@withContext false
        runCatching { Files.deleteIfExists(stagedPath) }.getOrDefault(false)
    }

    private fun preparePrivateDirectory(): Boolean = runCatching {
        Files.createDirectories(stagingDirectory.toPath())
        stagingDirectory.isDirectory && !Files.isSymbolicLink(stagingDirectory.toPath())
    }.getOrDefault(false)
}

internal fun interface ApkRequestExecutor {
    fun download(url: String, expectedBytes: Long, acceptChunk: (ByteArray, Int) -> Boolean): Boolean
}

private fun resolveTargetUrl(baseUrl: String, targetName: String): String? = runCatching {
    val baseUri = URI(baseUrl)
    if (
        baseUri.scheme != "https" || baseUri.host.isNullOrEmpty() || baseUri.userInfo != null ||
        baseUri.query != null || baseUri.fragment != null || !baseUri.path.endsWith('/') ||
        baseUri.port != -1 && baseUri.port !in 1..65535
    ) {
        return null
    }
    val resolvedUri = baseUri.resolve(targetName)
    if (
        resolvedUri.scheme != baseUri.scheme || resolvedUri.host != baseUri.host || resolvedUri.port != baseUri.port ||
        resolvedUri.userInfo != null || resolvedUri.query != null || resolvedUri.fragment != null ||
        !resolvedUri.path.startsWith(baseUri.path)
    ) {
        return null
    }
    resolvedUri.toASCIIString()
}.getOrNull()

private fun executeApkRequest(
    url: String,
    expectedBytes: Long,
    acceptChunk: (ByteArray, Int) -> Boolean,
): Boolean {
    val connection = runCatching { URI(url).toURL().openConnection() as HttpsURLConnection }.getOrNull() ?: return false
    return try {
        connection.requestMethod = "GET"
        connection.connectTimeout = CONNECT_TIMEOUT_MILLIS
        connection.readTimeout = APK_READ_TIMEOUT_MILLIS
        connection.instanceFollowRedirects = false
        connection.useCaches = false
        connection.setRequestProperty("Accept", "application/vnd.android.package-archive")
        connection.setRequestProperty("Accept-Encoding", "identity")
        connection.setRequestProperty("User-Agent", "Pixels-Android/1")
        if (connection.responseCode != 200 || connection.contentLengthLong != expectedBytes) return false
        val contentEncoding = connection.contentEncoding
        if (contentEncoding != null && !contentEncoding.equals("identity", ignoreCase = true)) return false
        connection.inputStream.use { input ->
            val payloadBuffer = ByteArray(64 * 1024)
            while (true) {
                val byteCount = input.read(payloadBuffer)
                if (byteCount < 0) break
                if (!acceptChunk(payloadBuffer, byteCount)) return false
            }
        }
        true
    } catch (_: Exception) {
        false
    } finally {
        connection.disconnect()
    }
}

private fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }

private const val MAXIMUM_APK_BYTES = 4L * 1024 * 1024 * 1024
private const val CONNECT_TIMEOUT_MILLIS = 5_000
private const val APK_READ_TIMEOUT_MILLIS = 60_000
