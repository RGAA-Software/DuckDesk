package yun.pixels.client.diagnostics

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Test

class DiagnosticsExporterTest {
    @Test
    fun sensitiveLinesAreRemovedCompletely() {
        assertEquals("[redacted sensitive diagnostic line]", redactDiagnosticsLine("Authorization: Bearer abc"))
        assertEquals("[redacted sensitive diagnostic line]", redactDiagnosticsLine("clipboard text: private"))
    }

    @Test
    fun networkAndSessionIdentifiersAreRedacted() {
        val value = redactDiagnosticsLine(
            "connect wss://pixels.example/media?ticket=abc via 192.168.31.6 session 123e4567-e89b-42d3-a456-426614174000",
        )

        assertEquals(
            "connect <url> via <ip-address> session <identifier>",
            value,
        )
        assertFalse(value.contains("ticket=abc"))
    }

    @Test
    fun userUrisAndFilePathsAreRedacted() {
        val value = redactDiagnosticsLine(
            "read content://yun.pixels.client.files/diagnostics/report.txt from /storage/emulated/0/Documents/report.txt",
        )

        assertEquals("read <content-uri> from <file-path>", value)
    }
}
