package yun.pixels.client.core.domain.recording

import org.junit.Assert.assertEquals
import org.junit.Assert.assertThrows
import org.junit.Test

class RecordingTest {
    @Test
    fun recordingIdRejectsBlankAndOversizedValues() {
        assertThrows(IllegalArgumentException::class.java) { RecordingId(" ") }
        assertThrows(IllegalArgumentException::class.java) { RecordingId("x".repeat(129)) }
    }

    @Test
    fun recordingStateRetainsMonotonicElapsedValue() {
        val id = RecordingId("recording-1")
        assertEquals(12_000L, RecordingState.Recording(id, 12_000L).elapsedMillis)
        assertEquals(12_500L, RecordingState.Stopping(id, 12_500L).elapsedMillis)
    }
}
