package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import java.util.Base64

class ConnectionInputParserTest {
    @Test
    fun parsesPrivateHostWithDefaultPorts() {
        val target = ConnectionInputParser.parse("192.168.50.12")

        assertEquals("192.168.50.12", target?.endpoints?.single()?.host)
        assertEquals(20369, target?.endpoints?.single()?.panelPort)
        assertEquals(20371, target?.endpoints?.single()?.renderPort)
    }

    @Test
    fun parsesLinkPayloadWithoutPersistingLegacyShapeInUi() {
        val json = """{"did":"desktop-1","dn":"Studio","rpwd":"418233","iidx":0,"ips":[{"ip":"192.168.1.8"}],"ppt":20369,"rdpt":20371}"""
        val encoded = Base64.getEncoder().encodeToString(json.toByteArray())

        val target = ConnectionInputParser.parse("link://$encoded")

        assertEquals("desktop-1", target?.expectedDeviceId)
        assertEquals("418233", target?.oneTimePassword)
        assertEquals("192.168.1.8", target?.endpoints?.single()?.host)
    }

    @Test
    fun rejectsPathsAndUnknownSchemes() {
        assertNull(ConnectionInputParser.parse("https://192.168.1.8"))
        assertNull(ConnectionInputParser.parse("http://192.168.1.8/admin"))
    }
}
