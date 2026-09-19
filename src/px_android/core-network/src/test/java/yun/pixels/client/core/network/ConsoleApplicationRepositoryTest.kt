package yun.pixels.client.core.network

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.account.RemoteApplication
import yun.pixels.client.core.domain.account.RemoteApplicationAccess
import yun.pixels.client.core.domain.account.RemoteApplicationInstance
import yun.pixels.client.core.domain.account.RemoteApplicationType

class ConsoleApplicationRepositoryTest {
    @Test
    fun terminalGuestHistoryDoesNotOccupyApplicationCard() {
        val application = application()
        val stopped = instance(RemoteApplicationInstance.State.Stopped, reconnectable = false)
        val failed = instance(RemoteApplicationInstance.State.Failed, reconnectable = false)

        val merged = mergeInstances(listOf(application), listOf(stopped, failed))

        assertNull(merged.single().runningInstance)
    }

    @Test
    fun activeGuestInstanceIsMergedIntoItsApplicationCard() {
        val application = application()
        val running = instance(RemoteApplicationInstance.State.Running, reconnectable = true)

        val merged = mergeInstances(listOf(application), listOf(running))

        assertEquals(running, merged.single().runningInstance)
    }

    private fun application() = RemoteApplication(
        appId = "app-1",
        name = "Cloud Game",
        coverUrl = "",
        type = RemoteApplicationType.GameHook,
        access = RemoteApplicationAccess.Public,
        version = 1,
        runningInstance = null,
    )

    private fun instance(state: RemoteApplicationInstance.State, reconnectable: Boolean) = RemoteApplicationInstance(
        instanceId = "instance-${state.name.lowercase()}",
        appId = "app-1",
        state = state,
        reconnectable = reconnectable,
    )
}
