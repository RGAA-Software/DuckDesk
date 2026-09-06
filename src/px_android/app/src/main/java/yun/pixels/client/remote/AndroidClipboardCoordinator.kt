package yun.pixels.client.remote

import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.core.content.FileProvider
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import yun.pixels.client.core.domain.session.ClipboardDownloadState
import yun.pixels.client.core.domain.session.LocalClipboardFile
import yun.pixels.client.core.domain.session.RemoteClipboardFiles
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.nativebridge.NativeRemoteSessionTransport
import java.io.File
import java.util.UUID

internal class AndroidClipboardCoordinator(
    context: Context,
    private val transport: NativeRemoteSessionTransport,
    private val scope: CoroutineScope,
) {
    private val applicationContext = context.applicationContext
    private val resolver = applicationContext.contentResolver
    private val clipboardManager = applicationContext.getSystemService(ClipboardManager::class.java)
    private val root = File(applicationContext.cacheDir, "clipboard")
    private var publishedGeneration: String? = null

    init {
        root.mkdirs()
        cleanupExpiredFiles()
    }

    fun share(sessionId: RemoteSessionId, uris: List<Uri>) {
        if (uris.isEmpty() || uris.size > MAX_FILE_COUNT) return
        scope.launch {
            val generation = UUID.randomUUID().toString()
            val files = withContext(Dispatchers.IO) { materializeUris(generation, uris) }
            if (files.isNotEmpty()) transport.sendClipboardFiles(sessionId, generation, files)
        }
    }

    fun request(sessionId: RemoteSessionId, files: RemoteClipboardFiles) {
        scope.launch {
            val destination = withContext(Dispatchers.IO) {
                File(root, "received/${files.generation}").apply {
                    deleteRecursively()
                    mkdirs()
                }.canonicalPath
            }
            transport.downloadClipboardFiles(sessionId, files.generation, destination)
        }
    }

    fun publish(state: ClipboardDownloadState.Ready) {
        if (publishedGeneration == state.generation) return
        runCatching {
            val files = state.localPaths.map(::File).filter(::isSafeReceivedFile)
            check(files.size == state.localPaths.size && files.isNotEmpty())
            val authority = "${applicationContext.packageName}.files"
            val uris = files.map { file -> FileProvider.getUriForFile(applicationContext, authority, file) }
            val clip = ClipData.newUri(resolver, "Pixels", uris.first())
            uris.drop(1).forEach { uri -> clip.addItem(ClipData.Item(uri)) }
            clipboardManager.setPrimaryClip(clip)
        }.onSuccess { publishedGeneration = state.generation }
    }

    private fun materializeUris(generation: String, uris: List<Uri>): List<LocalClipboardFile> {
        val directory = File(root, "shared/$generation").apply {
            deleteRecursively()
            mkdirs()
        }
        var totalBytes = 0L
        val result = mutableListOf<LocalClipboardFile>()
        uris.forEachIndexed { index, uri ->
            val displayName = resolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use { cursor ->
                if (cursor.moveToFirst()) cursor.getString(0) else null
            }.orEmpty().ifBlank { "pixels-clipboard-${index + 1}" }.sanitizeFileName()
            val destination = File(directory, "item-$index")
            val copied = resolver.openInputStream(uri)?.use { input ->
                destination.outputStream().use { output ->
                    val buffer = ByteArray(COPY_BUFFER_BYTES)
                    var size = 0L
                    while (true) {
                        val count = input.read(buffer)
                        if (count < 0) break
                        size += count
                        if (size > MAX_FILE_BYTES || totalBytes + size > MAX_TOTAL_BYTES) return@use -1L
                        output.write(buffer, 0, count)
                    }
                    size
                }
            } ?: -1L
            if (copied < 0L) {
                directory.deleteRecursively()
                return emptyList()
            }
            totalBytes += copied
            result += LocalClipboardFile(displayName, destination.canonicalPath, copied)
        }
        return result
    }

    private fun isSafeReceivedFile(file: File): Boolean {
        val receivedRoot = File(root, "received").canonicalFile
        val candidate = runCatching { file.canonicalFile }.getOrNull() ?: return false
        return candidate.isFile && candidate.toPath().startsWith(receivedRoot.toPath())
    }

    private fun cleanupExpiredFiles() {
        scope.launch(Dispatchers.IO) {
            val cutoff = System.currentTimeMillis() - CACHE_RETENTION_MILLIS
            root.listFiles()?.filter { it.lastModified() < cutoff }?.forEach(File::deleteRecursively)
        }
    }

    private fun String.sanitizeFileName(): String = replace(Regex("[\\\\/:*?\"<>|\\p{Cntrl}]"), "_").take(180)
        .ifBlank { "pixels-clipboard" }

    private companion object {
        const val MAX_FILE_COUNT = 16
        const val MAX_FILE_BYTES = 512L * 1024L * 1024L
        const val MAX_TOTAL_BYTES = 1024L * 1024L * 1024L
        const val COPY_BUFFER_BYTES = 128 * 1024
        const val CACHE_RETENTION_MILLIS = 24L * 60L * 60L * 1000L
    }
}
