package yun.pixels.client.core.nativebridge

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class NativeCallbackAttemptsTest {
    @Test(timeout = 5_000)
    fun queuedOldAttemptCannotChangeRetriedSession() = runBlocking {
        NativeCallbackAttempts(this).use { callbacks ->
            val seen = mutableListOf<String>()
            val old = callbacks.begin("same-session")
            callbacks.dispatch(old.callbackId) { seen += "old:$it" }
            callbacks.cancel(old)
            val current = callbacks.begin("same-session")
            assertNotEquals(old.callbackId, current.callbackId)
            callbacks.dispatch(current.callbackId) { seen += "current:$it" }
            callbacks.dispatch(old.callbackId) { seen += "late:$it" }
            yield()
            assertEquals(listOf("current:same-session"), seen)
            callbacks.finish(old)
            callbacks.finish(current)
        }
    }

    @Test(timeout = 5_000)
    fun stoppingCancelsSuspendedDelivery() = runBlocking {
        NativeCallbackAttempts(this).use { callbacks ->
            val attempt = callbacks.begin("session")
            val entered = CompletableDeferred<Unit>()
            val release = CompletableDeferred<Unit>()
            var delivered = false
            callbacks.dispatch(attempt.callbackId) {
                entered.complete(Unit)
                release.await()
                delivered = true
            }
            entered.await()
            callbacks.cancel(attempt)
            release.complete(Unit)
            yield()
            assertFalse(delivered)
            callbacks.finish(attempt)
        }
    }

    @Test(timeout = 5_000)
    fun recordingFinalizationSurvivesStopAndBindingRemoval() = runBlocking {
        NativeCallbackAttempts(this).use { callbacks ->
            val attempt = callbacks.begin("public-session")
            val seen = mutableListOf<String>()
            callbacks.cancel(attempt)
            callbacks.dispatchRecordingFinalization(attempt.callbackId) { seen += it }
            callbacks.finish(attempt)
            callbacks.dispatchRecordingFinalization(attempt.callbackId) { seen += "unknown:$it" }
            yield()
            assertEquals(listOf("public-session"), seen)
        }
    }

    @Test(timeout = 5_000)
    fun callbackCanStopItselfAndRepeatedCloseRejectsQueuedWork() = runBlocking {
        val callbacks = NativeCallbackAttempts(this)
        repeat(10) {
            val attempt = callbacks.begin("session-$it")
            val invoked = CompletableDeferred<Unit>()
            callbacks.dispatch(attempt.callbackId) {
                callbacks.finish(attempt)
                invoked.complete(Unit)
            }
            invoked.await()
            assertTrue(attempt.job.isCancelled)
        }
        val queued = callbacks.begin("queued")
        var delivered = false
        callbacks.dispatch(queued.callbackId) { delivered = true }
        callbacks.close()
        callbacks.close()
        assertTrue(runCatching { callbacks.begin("after-close") }.isFailure)
        yield()
        assertFalse(delivered)
    }
}
