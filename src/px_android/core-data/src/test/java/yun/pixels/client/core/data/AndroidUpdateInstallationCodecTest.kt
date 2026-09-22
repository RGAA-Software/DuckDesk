package yun.pixels.client.core.data

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationPhase
import yun.pixels.client.core.domain.update.AndroidUpdateInstallationRecord

class AndroidUpdateInstallationCodecTest {
    @Test
    fun canonicalInstallationRecordRoundTrips() {
        val record = installationRecord()

        assertEquals(record, AndroidUpdateInstallationCodec.decode(requireNotNull(AndroidUpdateInstallationCodec.encode(record))))
    }

    @Test
    fun malformedOrInconsistentInstallationRecordIsRejected() {
        val encodedRecord = requireNotNull(AndroidUpdateInstallationCodec.encode(installationRecord()))

        assertNull(AndroidUpdateInstallationCodec.decode(encodedRecord.replace("Submitted", "Unknown")))
        assertNull(AndroidUpdateInstallationCodec.decode(encodedRecord + "\nextra"))
        assertNull(
            AndroidUpdateInstallationCodec.encode(
                installationRecord().copy(phase = AndroidUpdateInstallationPhase.Failed, failureStatus = null),
            ),
        )
    }

    private fun installationRecord() = AndroidUpdateInstallationRecord(
        releaseId = "11111111-1111-4111-8111-111111111111",
        targetBuildNumber = 124,
        artifactSha256 = "11".repeat(32),
        sessionId = 7,
        phase = AndroidUpdateInstallationPhase.Submitted,
        failureStatus = null,
    )
}
