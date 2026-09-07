package yun.pixels.client.core.nativebridge

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.transfer.RemoteFileType

class RemoteDirectoryMappingTest {
    @Test
    fun mapsUtf8NamesAndKnownFileTypes() {
        val entries = remoteFileEntries(
            utf8Names = arrayOf("资料".encodeToByteArray(), "report.txt".encodeToByteArray()),
            entryTypes = intArrayOf(0, 4),
            utf8AbsolutePaths = arrayOf(ByteArray(0), ByteArray(0)),
            sizes = longArrayOf(0, 42),
            modifiedTimes = longArrayOf(1, 2),
        )

        requireNotNull(entries)
        assertEquals(listOf("资料", "report.txt"), entries.map { it.name })
        assertEquals(listOf(RemoteFileType.Directory, RemoteFileType.RegularFile), entries.map { it.type })
        assertEquals(42, entries.last().size)
    }

    @Test
    fun rejectsMismatchedNativeArrays() {
        assertNull(
            remoteFileEntries(
                utf8Names = arrayOf("file".encodeToByteArray()),
                entryTypes = intArrayOf(4),
                utf8AbsolutePaths = emptyArray(),
                sizes = longArrayOf(1),
                modifiedTimes = longArrayOf(1),
            ),
        )
    }
}
