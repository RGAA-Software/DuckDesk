package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.update.AndroidTufTrustedRoot
import yun.pixels.client.core.domain.update.AndroidTufMetadataWatermark

class TufTrustedRootStateCodecTest {
    @Test
    fun releaseIdentityVersionAndRootBytesRoundTripTogether() {
        val trustedRoot = AndroidTufTrustedRoot(
            distribution = "oem",
            releaseNamespace = "oem.acme-cloud",
            oemId = "acme-cloud",
            version = 7,
            rootBytes = "signed-root".toByteArray(),
            metadataWatermark = AndroidTufMetadataWatermark(8, "a".repeat(64), 7, "b".repeat(64), 6, "c".repeat(64)),
        )

        val decodedRoot = TufTrustedRootStateCodec.decode(requireNotNull(TufTrustedRootStateCodec.encode(trustedRoot)))

        assertEquals("oem", decodedRoot?.distribution)
        assertEquals("oem.acme-cloud", decodedRoot?.releaseNamespace)
        assertEquals("acme-cloud", decodedRoot?.oemId)
        assertEquals(7L, decodedRoot?.version)
        assertTrue(decodedRoot?.copyRootBytes()?.contentEquals(trustedRoot.copyRootBytes()) == true)
        assertEquals(trustedRoot.metadataWatermark, decodedRoot?.metadataWatermark)
    }

    @Test
    fun malformedOrCrossDomainStateIsRejected() {
        val officialRoot = AndroidTufTrustedRoot(
            distribution = "official",
            releaseNamespace = "pixels.customer",
            oemId = null,
            version = 1,
            rootBytes = byteArrayOf(1),
            metadataWatermark = null,
        )

        assertNull(TufTrustedRootStateCodec.encode(officialRoot))
        assertNull(TufTrustedRootStateCodec.decode("2\nofficial\npixels.official\n\n0\nAQ==\n\n\n\n\n\n"))
        assertNull(TufTrustedRootStateCodec.decode("2\nofficial\npixels.official\n\n1\nnot-base64\n\n\n\n\n\n"))
    }

    @Test
    fun rootWithoutAcceptedOnlineMetadataRoundTrips() {
        val trustedRoot = AndroidTufTrustedRoot(
            distribution = "customer",
            releaseNamespace = "pixels.customer",
            oemId = null,
            version = 1,
            rootBytes = byteArrayOf(1, 2, 3),
            metadataWatermark = null,
        )

        val decodedRoot = TufTrustedRootStateCodec.decode(requireNotNull(TufTrustedRootStateCodec.encode(trustedRoot)))

        assertEquals(1L, decodedRoot?.version)
        assertNull(decodedRoot?.metadataWatermark)
    }
}
