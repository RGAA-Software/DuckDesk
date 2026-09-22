package yun.pixels.client.update

import android.content.pm.PackageInfo
import android.content.pm.PackageManager
import android.os.Build
import java.security.MessageDigest
import yun.pixels.client.core.domain.update.PreparedAndroidUpdate
import yun.pixels.client.core.domain.update.PreparedAndroidUpdateVerifier

class AndroidApkPlatformVerifier(
    private val packageManager: PackageManager,
    private val expectedPackageName: String,
    private val installedVersionCode: Long,
) : PreparedAndroidUpdateVerifier {
    override fun verify(preparedUpdate: PreparedAndroidUpdate): Boolean {
        val packageInfo = readArchivePackageInfo(preparedUpdate.stagedApkPath) ?: return false
        val signingInfo = packageInfo.signingInfo ?: return false
        val signingCertificates = signingInfo.apkContentsSigners ?: return false
        if (signingCertificates.size != 1) return false
        val archiveIdentity = AndroidArchiveIdentity(
            packageName = packageInfo.packageName,
            versionCode = packageInfo.longVersionCode,
            versionName = packageInfo.versionName,
            signerSha256 = MessageDigest.getInstance("SHA-256").digest(signingCertificates.single().toByteArray()).toHex(),
        )
        return archiveIdentity.matches(preparedUpdate, expectedPackageName, installedVersionCode)
    }

    @Suppress("DEPRECATION")
    private fun readArchivePackageInfo(apkPath: String): PackageInfo? = runCatching {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            packageManager.getPackageArchiveInfo(
                apkPath,
                PackageManager.PackageInfoFlags.of(PackageManager.GET_SIGNING_CERTIFICATES.toLong()),
            )
        } else {
            packageManager.getPackageArchiveInfo(apkPath, PackageManager.GET_SIGNING_CERTIFICATES)
        }
    }.getOrNull()
}

internal data class AndroidArchiveIdentity(
    val packageName: String,
    val versionCode: Long,
    val versionName: String?,
    val signerSha256: String,
) {
    fun matches(
        preparedUpdate: PreparedAndroidUpdate,
        expectedPackageName: String,
        installedVersionCode: Long,
    ): Boolean {
        val artifact = preparedUpdate.release.artifact
        return packageName == expectedPackageName &&
            versionCode == artifact.buildNumber &&
            versionCode > installedVersionCode &&
            versionName == artifact.version &&
            signerSha256 == artifact.platformSignerSha256
    }
}

private fun ByteArray.toHex(): String = joinToString("") { byte -> "%02x".format(byte.toInt() and 0xff) }
