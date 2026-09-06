package yun.pixels.client.feature.transfer

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.transfer.RemoteFileEntry
import yun.pixels.client.core.domain.transfer.RemoteFileType

class RemotePathTest {
    @Test
    fun parentNavigatesAcrossWindowsRoots() {
        assertEquals("C:/Users", remoteParentPath("C:\\Users\\Public"))
        assertEquals("C:/", remoteParentPath("C:/Users"))
        assertEquals("/", remoteParentPath("C:/"))
        assertNull(remoteParentPath("/"))
    }

    @Test
    fun entryUsesAbsolutePathWhenServerProvidesOne() {
        val pinned = entry("Downloads", "D:/Profiles/A/Downloads")
        val child = entry("Documents")

        assertEquals("D:/Profiles/A/Downloads", remoteEntryPath("/", pinned))
        assertEquals("C:/Users/A/Documents", remoteEntryPath("C:/Users/A", child))
    }

    private fun entry(name: String, absolutePath: String = "") = RemoteFileEntry(
        name = name,
        absolutePath = absolutePath,
        type = RemoteFileType.Directory,
        size = 0,
        modifiedTimeEpochSeconds = 0,
    )
}
