package yun.pixels.client.remote

import org.junit.Assert.assertEquals
import org.junit.Test

class AndroidFileTransferPathTest {
    @Test
    fun uploadDestinationIncludesTheSelectedFileName() {
        assertEquals(
            "C:/Users/Public/Downloads/report.txt",
            remoteDestinationPath("C:/Users/Public/Downloads/", "report.txt"),
        )
        assertEquals("/tmp/report.txt", remoteDestinationPath("/tmp", "report.txt"))
    }
}
