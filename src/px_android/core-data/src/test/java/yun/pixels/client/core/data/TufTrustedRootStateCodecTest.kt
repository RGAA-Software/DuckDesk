package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.update.AndroidTufTrustedRoot

class TufTrustedRootStateCodecTest {
    @Test
    fun releaseIdentityVersionAndRootBytesRoundTripTogether() {
        val trustedRoot = AndroidTufTrustedRoot(
            distribution = "oem",
            releaseNamespace = "oem.acme-cloud",
            oemId = "acme-cloud",
            version = 7,
            rootBytes = "signed-root".toByteArray(),
        )

        val decodedRoot = TufTrustedRootStateCodec.decode(requireNotNull(TufTrustedRootStateCodec.encode(trustedRoot)))

        assertEquals("oem", decodedRoot?.distribution)
        assertEquals("oem.acme-cloud", decodedRoot?.releaseNamespace)
        assertEquals("acme-cloud", decodedRoot?.oemId)
        assertEquals(7L, decodedRoot?.version)
        assertTrue(decodedRoot?.copyRootBytes()?.contentEquals(trustedRoot.copyRootBytes()) == true)
    }

    @Test
    fun malformedOrCrossDomainStateIsRejected() {
        val officialRoot = AndroidTufTrustedRoot(
            distribution = "official",
            releaseNamespace = "pixels.customer",
            oemId = null,
            version = 1,
            rootBytes = byteArrayOf(1),
        )

        assertNull(TufTrustedRootStateCodec.encode(officialRoot))
        assertNull(TufTrustedRootStateCodec.decode("1\nofficial\npixels.official\n\n0\nAQ=="))
        assertNull(TufTrustedRootStateCodec.decode("1\nofficial\npixels.official\n\n1\nnot-base64"))
    }
}
