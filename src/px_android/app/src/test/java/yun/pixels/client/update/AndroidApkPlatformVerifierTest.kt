package yun.pixels.client.update

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import yun.pixels.client.core.domain.update.AndroidUpdateArtifact
import yun.pixels.client.core.domain.update.AndroidUpdateRelease
import yun.pixels.client.core.domain.update.PreparedAndroidUpdate

class AndroidApkPlatformVerifierTest {
    @Test
    fun exactNewerArchiveIdentityIsAccepted() {
        assertTrue(approvedIdentity().matches(preparedUpdate(), EXPECTED_PACKAGE_NAME, INSTALLED_VERSION_CODE))
    }

    @Test
    fun mismatchedPackageVersionOrSignerIsRejected() {
        val preparedUpdate = preparedUpdate()

        assertFalse(
            approvedIdentity().copy(packageName = "yun.pixels.client.customer")
                .matches(preparedUpdate, EXPECTED_PACKAGE_NAME, INSTALLED_VERSION_CODE),
        )
        assertFalse(
            approvedIdentity().copy(versionCode = TARGET_VERSION_CODE + 1)
                .matches(preparedUpdate, EXPECTED_PACKAGE_NAME, INSTALLED_VERSION_CODE),
        )
        assertFalse(
            approvedIdentity().copy(versionName = "9.9.9")
                .matches(preparedUpdate, EXPECTED_PACKAGE_NAME, INSTALLED_VERSION_CODE),
        )
        assertFalse(
            approvedIdentity().copy(signerSha256 = "22".repeat(32))
                .matches(preparedUpdate, EXPECTED_PACKAGE_NAME, INSTALLED_VERSION_CODE),
        )
    }

    @Test
    fun nonIncreasingVersionIsRejected() {
        assertFalse(approvedIdentity().matches(preparedUpdate(), EXPECTED_PACKAGE_NAME, TARGET_VERSION_CODE))
    }

    private fun approvedIdentity() = AndroidArchiveIdentity(
        packageName = EXPECTED_PACKAGE_NAME,
        versionCode = TARGET_VERSION_CODE,
        versionName = "1.2.3",
        signerSha256 = "11".repeat(32),
    )

    private fun preparedUpdate() = PreparedAndroidUpdate(
        release = AndroidUpdateRelease(
            releaseId = "11111111-1111-4111-8111-111111111111",
            repositoryPublicationSha256 = "33".repeat(32),
            repositoryRootVersion = 1,
            revision = 1,
            createdAtEpochMillis = 1,
            updatedAtEpochMillis = 1,
            artifact = AndroidUpdateArtifact(
                buildNumber = TARGET_VERSION_CODE,
                version = "1.2.3",
                metadataBaseUrl = "https://updates.example/metadata/",
                targetsBaseUrl = "https://updates.example/targets/",
                targetName = "android/android/official/stable/aarch64/123/pixels.apk",
                sha256 = "44".repeat(32),
                platformSignerSha256 = "11".repeat(32),
                sizeBytes = 1024,
            ),
        ),
        stagedApkPath = "/private/prepared.apk",
    )

    private companion object {
        const val EXPECTED_PACKAGE_NAME = "yun.pixels.client"
        const val INSTALLED_VERSION_CODE = 122L
        const val TARGET_VERSION_CODE = 123L
    }
}
