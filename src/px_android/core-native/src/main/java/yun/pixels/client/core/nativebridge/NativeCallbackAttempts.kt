package yun.pixels.client.core.nativebridge

import java.io.Closeable
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/** JNI callback identities are local attempt IDs, never the reused UI session ID or a wire credential. */
internal class NativeCallbackAttempts(private val scope: CoroutineScope) : Closeable {
    internal class Attempt(val sessionId: String, parent: Job?) {
        val callbackId: String = UUID.randomUUID().toString()
        val job = SupervisorJob(parent)
    }

    private val attempts = ConcurrentHashMap<String, Attempt>()
    private val lifecycle = Any()
    private var closed = false

    fun begin(sessionId: String): Attempt = synchronized(lifecycle) {
        check(!closed) { "Native callback dispatcher is closed" }
        Attempt(sessionId, scope.coroutineContext[Job]).also { attempts[it.callbackId] = it }
    }

    fun cancel(attempt: Attempt) { attempt.job.cancel() }

    fun finish(attempt: Attempt) {
        attempts.remove(attempt.callbackId, attempt)
        attempt.job.cancel()
    }

    fun dispatch(callbackId: String, action: suspend (String) -> Unit) {
        val attempt = attempts[callbackId] ?: return
        scope.launch(attempt.job) { action(attempt.sessionId) }
    }

    // Native Stop drains recording workers before finish removes the binding. Capture the public
    // ID now; the recording coordinator independently matches its immutable recording ID.
    fun dispatchRecordingFinalization(callbackId: String, action: suspend (String) -> Unit) {
        val sessionId = attempts[callbackId]?.sessionId ?: return
        scope.launch { action(sessionId) }
    }

    override fun close() {
        val retiring = synchronized(lifecycle) {
            if (closed) return
            closed = true
            attempts.values.toList().also { attempts.clear() }
        }
        retiring.forEach { it.job.cancel() }
    }
}
