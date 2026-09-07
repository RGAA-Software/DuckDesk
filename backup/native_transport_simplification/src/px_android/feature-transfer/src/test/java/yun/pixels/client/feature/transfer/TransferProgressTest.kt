package yun.pixels.client.feature.transfer

import org.junit.Assert.assertEquals
import org.junit.Test
import yun.pixels.client.core.domain.session.RemoteSessionId
import yun.pixels.client.core.domain.transfer.FileTransferDirection
import yun.pixels.client.core.domain.transfer.FileTransferTask

class TransferProgressTest {
    @Test
    fun progressIsBoundedAndUnknownTotalsAreIndeterminate() {
        val base = FileTransferTask("task", RemoteSessionId("session"), name = "file", direction = FileTransferDirection.Upload)
        assertEquals(null, base.progress)
        assertEquals(0.5f, base.copy(totalBytes = 100, completedBytes = 50).progress)
        assertEquals(1f, base.copy(totalBytes = 100, completedBytes = 500).progress)
    }
}
