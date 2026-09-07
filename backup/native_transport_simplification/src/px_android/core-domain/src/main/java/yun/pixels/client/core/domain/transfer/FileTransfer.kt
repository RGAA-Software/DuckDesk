package yun.pixels.client.core.domain.transfer

import kotlinx.coroutines.flow.Flow
import yun.pixels.client.core.domain.session.RemoteSessionId

enum class FileTransferDirection {
    Upload,
    Download,
}

enum class FileTransferState {
    Preparing,
    Queued,
    Running,
    AwaitingOverwrite,
    Succeeded,
    Failed,
    Cancelled,
}

data class FileTransferTask(
    val taskId: String,
    val sessionId: RemoteSessionId,
    val nativeJobId: Int? = null,
    val name: String,
    val direction: FileTransferDirection,
    val state: FileTransferState = FileTransferState.Preparing,
    val fileNumber: Int = 0,
    val fileCount: Int = 0,
    val totalBytes: Long = 0,
    val completedBytes: Long = 0,
    val speedBytesPerSecond: Double = 0.0,
    val error: String = "",
)

enum class RemoteFileType {
    Directory,
    DirectoryLink,
    Drive,
    RegularFile,
    FileLink,
}

data class RemoteFileEntry(
    val name: String,
    val absolutePath: String,
    val type: RemoteFileType,
    val size: Long,
    val modifiedTimeEpochSeconds: Long,
) {
    val isDirectory: Boolean
        get() = when (type) {
            RemoteFileType.Directory, RemoteFileType.DirectoryLink, RemoteFileType.Drive -> true
            RemoteFileType.RegularFile, RemoteFileType.FileLink -> false
        }
}

sealed interface RemoteDirectoryState {
    data object Idle : RemoteDirectoryState

    data class Loading(val path: String) : RemoteDirectoryState

    data class Ready(
        val path: String,
        val entries: List<RemoteFileEntry>,
        val truncated: Boolean,
    ) : RemoteDirectoryState

    data class Failed(val path: String, val reason: String) : RemoteDirectoryState
}

data class RemoteDirectoryEvent(
    val sessionId: RemoteSessionId,
    val path: String,
    val entries: List<RemoteFileEntry>,
    val truncated: Boolean,
)

sealed interface FileTransferEvent {
    val sessionId: RemoteSessionId
    val jobId: Int

    data class Progress(
        override val sessionId: RemoteSessionId,
        override val jobId: Int,
        val fileNumber: Int,
        val fileCount: Int,
        val totalBytes: Long,
        val completedBytes: Long,
        val transferredBytes: Long,
        val speedBytesPerSecond: Double,
        val direction: FileTransferDirection,
    ) : FileTransferEvent

    data class Completed(
        override val sessionId: RemoteSessionId,
        override val jobId: Int,
        val error: String,
    ) : FileTransferEvent

    data class OverwriteRequired(
        override val sessionId: RemoteSessionId,
        override val jobId: Int,
        val fileNumber: Int,
        val path: String,
        val upload: Boolean,
        val identical: Boolean,
    ) : FileTransferEvent
}

interface FileTransferTransport {
    val fileTransferEvents: Flow<FileTransferEvent>
    val remoteDirectoryEvents: Flow<RemoteDirectoryEvent>

    suspend fun listRemoteDirectory(sessionId: RemoteSessionId, path: String): Boolean

    suspend fun startUpload(sessionId: RemoteSessionId, localPath: String, remoteDirectory: String): Int?

    suspend fun startDownload(sessionId: RemoteSessionId, remotePath: String, localDirectory: String): Int?

    suspend fun cancelTransfer(sessionId: RemoteSessionId, jobId: Int): Boolean

    suspend fun confirmOverwrite(
        sessionId: RemoteSessionId,
        jobId: Int,
        fileNumber: Int,
        overwrite: Boolean,
        offsetBytes: Long = 0,
        applyToAll: Boolean = false,
    ): Boolean
}
