package yun.pixels.client.core.nativebridge

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withContext
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.account.AccountFailure
import yun.pixels.client.core.domain.account.AccountResult
import yun.pixels.client.core.domain.account.ConnectionTicket
import yun.pixels.client.core.domain.account.JoinMode

class ConnectionTicketAttemptTest {
    @Test(timeout = 5_000)
    fun everyFurtherAttemptUsesLatestRotatingCapability() = runBlocking {
        val attempt = ConnectionTicketAttempt(ticket(0))
        val seen = mutableListOf<String>()
        repeat(4) { round ->
            val result = attempt.prepareForStart(1_000_000L, "nonce") { current, nonce ->
                assertEquals("nonce", nonce)
                seen += current.renewalToken
                AccountResult.Success(ticket(round))
            }
            assertEquals(AccountResult.Success(ticket(round)), result)
            assertTrue(attempt.requiresRenewal(1_000_000L))
        }
        assertEquals(listOf("renewal-0", "renewal-1", "renewal-2"), seen)
    }

    @Test(timeout = 5_000)
    fun expiringTicketIsRenewedBeforeFirstAttempt() = runBlocking {
        val attempt = ConnectionTicketAttempt(ticket(0).copy(expiresAtEpochMillis = 1_015_000L))
        val result = attempt.prepareForStart(1_000_000L, "nonce") { _, _ -> AccountResult.Success(ticket(1)) }
        assertEquals(AccountResult.Success(ticket(1)), result)
        assertEquals(ticket(1), attempt.current)
    }

    @Test(timeout = 5_000)
    fun failedRenewalRetainsPreviousCapabilityForRetry() = runBlocking {
        val attempt = ConnectionTicketAttempt(ticket(0))
        attempt.markAttempted()
        val result = attempt.prepareForStart(1_000_000L, "nonce") { _, _ -> AccountResult.Failure(AccountFailure.NetworkUnavailable) }
        assertEquals(AccountResult.Failure(AccountFailure.NetworkUnavailable), result)
        assertEquals(ticket(0), attempt.current)
        assertTrue(attempt.requiresRenewal(1_000_000L))
    }

    @Test(timeout = 5_000)
    fun cancelledLateRenewalRetainsFreshTicketButNeverStartsNative() = runBlocking {
        val attempt = ConnectionTicketAttempt(ticket(0).copy(expiresAtEpochMillis = 1_000_000L))
        val entered = CompletableDeferred<Unit>()
        val release = CompletableDeferred<Unit>()
        var nativeStarted = false
        val task = launch {
            attempt.prepareForStart(1_000_000L, "nonce") { _, _ ->
                withContext(NonCancellable) {
                    entered.complete(Unit)
                    release.await()
                    AccountResult.Success(ticket(1))
                }
            }
            nativeStarted = true
        }
        entered.await()
        task.cancel()
        release.complete(Unit)
        task.join()
        assertTrue(task.isCancelled)
        assertFalse(nativeStarted)
        assertEquals(ticket(1), attempt.current)
        assertFalse(attempt.requiresRenewal(1_000_000L))
    }

    private fun ticket(round: Int) = ConnectionTicket(
        ticket = "ticket-$round",
        renewalToken = "renewal-$round",
        launchUrl = "https://edge.example.com/web_client/",
        expiresAtEpochMillis = Long.MAX_VALUE,
        logicalSessionId = "logical-session",
        streamId = "runtime-stream",
        joinMode = JoinMode.Control,
        permissions = setOf("view"),
        rtcIceConfigJson = "",
        relayHost = "",
        relayPort = 0,
        signalDeviceId = "remote-device",
    )
}
