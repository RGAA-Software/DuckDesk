package yun.pixels.client.remote

import android.content.Context
import android.net.Uri
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.runBlocking
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.transfer.FileTransferDirection
import yun.pixels.client.core.domain.transfer.FileTransferEvent
import yun.pixels.client.core.domain.transfer.FileTransferState
import yun.pixels.client.core.domain.transfer.FileTransferTask
import yun.pixels.client.core.domain.transfer.FileTransferTransport
import yun.pixels.client.core.domain.transfer.RemoteDirectoryEvent

@RunWith(AndroidJUnit4::class)
class AndroidFileTransferCoordinatorTest {
    private lateinit var scope: CoroutineScope

    @Before
    fun setUp() {
        scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    }

    @After
    fun tearDown() {
        scope.cancel()
    }

    @Test
    fun failedDownloadRetryStartsANewCleanNativeJob() = runBlocking {
        val context = ApplicationProvider.getApplicationContext<Context>()
        val transport = RecordingFileTransferTransport()
        val coordinator = AndroidFileTransferCoordinator(context, transport, scope)
        val sessionId = RemoteSessionId("retry-session")

        coordinator.download(
            sessionId = sessionId,
            remotePath = "C:/exports/report.bin",
            destination = Uri.parse("content://pixels.test/report.bin"),
        )
        val firstRunning = awaitTask(coordinator, FileTransferState.Running)
        assertEquals(41, firstRunning.nativeJobId)

        transport.emit(
            FileTransferEvent.Progress(
                sessionId = sessionId,
                jobId = 41,
                fileNumber = 2,
                fileCount = 3,
                totalBytes = 4096,
                completedBytes = 2048,
                transferredBytes = 2048,
                speedBytesPerSecond = 512.0,
                direction = FileTransferDirection.Download,
            ),
        )
        awaitTask(coordinator) { it.completedBytes == 2048L }
        transport.emit(FileTransferEvent.Completed(sessionId, 41, "network unavailable"))
        val failed = awaitTask(coordinator, FileTransferState.Failed)

        coordinator.retry(failed.taskId)
        val retried = awaitTask(coordinator) { it.state == FileTransferState.Running && it.nativeJobId == 42 }

        assertEquals(failed.taskId, retried.taskId)
        assertEquals(listOf(41, 42), transport.startedJobIds)
        assertEquals(0, retried.fileNumber)
        assertEquals(0, retried.fileCount)
        assertEquals(0, retried.totalBytes)
        assertEquals(0, retried.completedBytes)
        assertEquals(0.0, retried.speedBytesPerSecond, 0.0)
        assertTrue(retried.error.isEmpty())
    }

    private suspend fun awaitTask(
        coordinator: AndroidFileTransferCoordinator,
        state: FileTransferState,
    ) = awaitTask(coordinator) { it.state == state }

    private suspend fun awaitTask(
        coordinator: AndroidFileTransferCoordinator,
        predicate: (FileTransferTask) -> Boolean,
    ): FileTransferTask {
        repeat(100) {
            coordinator.tasks.value.firstOrNull(predicate)?.let { return it }
            delay(25)
        }
        throw AssertionError("Timed out waiting for the expected file transfer state")
    }

    private class RecordingFileTransferTransport : FileTransferTransport {
        private val mutableFileTransferEvents = MutableSharedFlow<FileTransferEvent>(extraBufferCapacity = 8)
        private val mutableRemoteDirectoryEvents = MutableSharedFlow<RemoteDirectoryEvent>(extraBufferCapacity = 1)
        private var nextJobId = 41

        val startedJobIds = mutableListOf<Int>()
        override val fileTransferEvents: Flow<FileTransferEvent> = mutableFileTransferEvents
        override val remoteDirectoryEvents: Flow<RemoteDirectoryEvent> = mutableRemoteDirectoryEvents

        suspend fun emit(event: FileTransferEvent) {
            mutableFileTransferEvents.emit(event)
        }

        override suspend fun listRemoteDirectory(sessionId: RemoteSessionId, path: String) = true

        override suspend fun startUpload(sessionId: RemoteSessionId, localPath: String, remoteDirectory: String): Int? = null

        override suspend fun startDownload(sessionId: RemoteSessionId, remotePath: String, localDirectory: String): Int {
            val jobId = nextJobId++
            startedJobIds += jobId
            return jobId
        }

        override suspend fun cancelTransfer(sessionId: RemoteSessionId, jobId: Int) = true

        override suspend fun confirmOverwrite(
            sessionId: RemoteSessionId,
            jobId: Int,
            fileNumber: Int,
            overwrite: Boolean,
            offsetBytes: Long,
            applyToAll: Boolean,
        ) = true
    }
}
