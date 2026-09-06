package yun.pixels.client.core.domain.session

import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.test.advanceUntilIdle
import kotlinx.coroutines.test.runTest
import kotlinx.coroutines.test.UnconfinedTestDispatcher
import kotlinx.coroutines.withTimeout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Assert.assertThrows
import org.junit.Test
import yun.pixels.client.core.domain.device.DeviceAvailability
import yun.pixels.client.core.domain.device.DeviceEndpoint
import yun.pixels.client.core.domain.device.DeviceId
import yun.pixels.client.core.domain.device.RemoteDevice

@OptIn(ExperimentalCoroutinesApi::class)
class RemoteSessionWorkflowTest {
    @Test
    fun textInputEnforcesTheProtocolUtf8Limit() {
        assertEquals("桌面 input", InputCommand.Text("桌面 input").value)
        assertThrows(IllegalArgumentException::class.java) { InputCommand.Text("") }
        assertThrows(IllegalArgumentException::class.java) { InputCommand.Text("界".repeat(1_366)) }
    }

    @Test
    fun desktopInputCommandsRejectInvalidCoordinatesAndEmptyWheelEvents() {
        assertEquals(InputCommand.MoveAbsolute(0.25f, 0.75f), InputCommand.MoveAbsolute(0.25f, 0.75f))
        assertThrows(IllegalArgumentException::class.java) { InputCommand.MoveAbsolute(-0.1f, 0.5f) }
        assertThrows(IllegalArgumentException::class.java) { InputCommand.MoveRelative(Float.NaN, 0f) }
        assertThrows(IllegalArgumentException::class.java) { InputCommand.MouseButton(RemoteMouseButton.Left, true, 0.5f, null) }
        assertThrows(IllegalArgumentException::class.java) { InputCommand.Wheel(0, 0) }
    }

    @Test
    fun repeatedStartForSameSessionIsIdempotent() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val request = request("session-1")

        workflow.start(request)
        workflow.start(request)

        assertEquals(listOf(request), transport.starts)
    }

    @Test
    fun startingAnotherSessionStopsTheFirstBeforeStart() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val first = request("session-1")
        val second = request("session-2")

        workflow.start(first)
        workflow.start(second)

        assertEquals(listOf(first.id), transport.stops)
        assertEquals(listOf(first, second), transport.starts)
        assertEquals(RemoteSessionStatus.Starting(second), workflow.snapshot.value.status)
    }

    @Test
    fun queuedEventFromStoppedSessionIsIgnored() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val first = request("session-1")
        val second = request("session-2")
        workflow.start(first)
        workflow.start(second)

        transport.eventsFlow.emit(RemoteTransportEvent.Connected(first.id, capabilities()))
        advanceUntilIdle()

        assertEquals(RemoteSessionStatus.Starting(second), workflow.snapshot.value.status)
    }

    @Test
    fun terminalDisconnectStopsTransportAndKeepsTypedFailure() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val request = request("session-1")
        workflow.start(request)

        transport.eventsFlow.emit(RemoteTransportEvent.Disconnected(request.id, RemoteSessionFailure.AuthenticationRejected, false))
        advanceUntilIdle()
        transport.eventsFlow.emit(RemoteTransportEvent.Disconnected(request.id, RemoteSessionFailure.NetworkUnavailable, true))
        advanceUntilIdle()

        assertEquals(listOf(request.id), transport.stops)
        assertEquals(RemoteSessionStatus.Failed(request, RemoteSessionFailure.AuthenticationRejected), workflow.snapshot.value.status)
    }

    @Test
    fun closeIsSafeAfterRepeatedStopAndDropsLaterCallbacks() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val request = request("session-1")
        workflow.start(request)

        workflow.stop()
        workflow.stop()
        workflow.close()
        transport.eventsFlow.emit(RemoteTransportEvent.Connected(request.id, capabilities()))
        advanceUntilIdle()

        assertEquals(listOf(request.id), transport.stops)
        assertTrue(workflow.snapshot.value.status is RemoteSessionStatus.Idle)
    }

    @Test
    fun disconnectCallbackDuringStopDoesNotDeadlockOrRestoreFailedState() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val request = request("session-1")
        workflow.start(request)
        transport.onStop = { sessionId ->
            transport.eventsFlow.emit(RemoteTransportEvent.Disconnected(sessionId, RemoteSessionFailure.RemoteEnded, false))
        }

        withTimeout(1_000) { workflow.stop() }

        assertEquals(listOf(request.id), transport.stops)
        assertTrue(workflow.snapshot.value.status is RemoteSessionStatus.Idle)
    }

    @Test
    fun videoAndStatisticsEventsUpdateTheActiveSessionWithoutReplacingItsStatus() = runTest {
        val transport = FakeRemoteSessionTransport()
        val workflow = RemoteSessionWorkflow(transport, CoroutineScope(UnconfinedTestDispatcher(testScheduler)))
        val request = request("session-1")
        val statistics = RemoteSessionStatistics(framesPerSecond = 58, latencyMillis = 17, bitrateKbps = 12_400)
        val videoSize = RemoteVideoSize(2560, 1440)
        workflow.start(request)
        transport.eventsFlow.emit(RemoteTransportEvent.Connected(request.id, capabilities()))
        transport.eventsFlow.emit(RemoteTransportEvent.Statistics(request.id, statistics))
        transport.eventsFlow.emit(RemoteTransportEvent.VideoSize(request.id, videoSize))
        advanceUntilIdle()

        assertEquals(RemoteSessionStatus.Connected(request, capabilities()), workflow.snapshot.value.status)
        assertEquals(statistics, workflow.snapshot.value.statistics)
        assertEquals(videoSize, workflow.snapshot.value.videoSize)
    }

    private fun request(id: String) = RemoteSessionRequest(
        id = RemoteSessionId(id),
        target = RemoteSessionTarget.Direct(
            device = RemoteDevice(
                id = DeviceId("device-1"),
                displayName = "Office PC",
                platformName = "Windows",
                availability = DeviceAvailability.Online,
                endpoint = DeviceEndpoint("192.168.1.8"),
            ),
            credential = "credential",
        ),
    )

    private fun capabilities() = RemoteSessionCapabilities(
        monitorNames = listOf("DISPLAY1"),
        activeMonitorName = "DISPLAY1",
        supportsAudio = true,
        supportsInput = true,
        supportsFileTransfer = true,
        supportsClipboard = true,
    )
}

private class FakeRemoteSessionTransport : RemoteSessionTransport {
    val eventsFlow = MutableSharedFlow<RemoteTransportEvent>(replay = 1, extraBufferCapacity = 8)
    override val events = eventsFlow
    val starts = mutableListOf<RemoteSessionRequest>()
    val stops = mutableListOf<RemoteSessionId>()
    var startResult: RemoteTransportStartResult = RemoteTransportStartResult.Accepted
    var onStop: suspend (RemoteSessionId) -> Unit = {}

    override suspend fun start(request: RemoteSessionRequest): RemoteTransportStartResult {
        starts += request
        return startResult
    }

    override suspend fun stop(sessionId: RemoteSessionId) {
        stops += sessionId
        onStop(sessionId)
    }

    override suspend fun sendInput(sessionId: RemoteSessionId, command: InputCommand): Boolean = true
}
