package yun.pixels.client.diagnostics

import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Process
import androidx.core.content.FileProvider
import java.io.File
import java.time.Instant
import java.util.Locale
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import yun.pixels.client.BuildConfig

object DiagnosticsExporter {
    suspend fun create(context: Context, sessionState: String): Uri = withContext(Dispatchers.IO) {
        val applicationContext = context.applicationContext
        val directory = File(applicationContext.cacheDir, DIAGNOSTICS_DIRECTORY)
        check(directory.exists() || directory.mkdirs()) { "Unable to create diagnostics directory" }
        directory.listFiles()?.forEach { file ->
            if (file.isFile && file.lastModified() < System.currentTimeMillis() - DIAGNOSTICS_RETENTION_MILLIS) file.delete()
        }
        val report = File(directory, "pixels-diagnostics-${System.currentTimeMillis()}.txt")
        report.bufferedWriter().use { writer ->
            writer.appendLine("Pixels Android diagnostics")
            writer.appendLine("Generated: ${Instant.now()}")
            writer.appendLine("Version: ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE})")
            writer.appendLine("Build: ${BuildConfig.BUILD_TYPE} ${BuildConfig.GIT_REVISION}")
            writer.appendLine("Android: ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
            writer.appendLine("Device: ${Build.MANUFACTURER} ${Build.MODEL}")
            writer.appendLine("Locale: ${Locale.getDefault().toLanguageTag()}")
            writer.appendLine("Session state: $sessionState")
            writer.appendLine()
            writer.appendLine("Recent application logs (sensitive values redacted):")
            writer.append(readRecentApplicationLogs())
        }
        FileProvider.getUriForFile(applicationContext, "${applicationContext.packageName}.files", report)
    }

    fun share(context: Context, report: Uri, chooserTitle: String) {
        val intent = Intent(Intent.ACTION_SEND)
            .setType("text/plain")
            .putExtra(Intent.EXTRA_STREAM, report)
            .addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
        context.startActivity(Intent.createChooser(intent, chooserTitle))
    }

    private fun readRecentApplicationLogs(): String = runCatching {
        val command = listOf("logcat", "-d", "-t", MAX_LOG_LINES.toString(), "-v", "threadtime", "--pid=${Process.myPid()}")
        val process = ProcessBuilder(command).redirectErrorStream(true).start()
        process.inputStream.bufferedReader().useLines { lines ->
            lines.joinToString(separator = "\n", postfix = "\n", transform = ::redactDiagnosticsLine)
        }.takeLast(MAX_LOG_CHARACTERS)
    }.getOrElse { "Application logs unavailable.\n" }

    private const val DIAGNOSTICS_DIRECTORY = "diagnostics"
    private const val DIAGNOSTICS_RETENTION_MILLIS = 24L * 60L * 60L * 1_000L
    private const val MAX_LOG_LINES = 300
    private const val MAX_LOG_CHARACTERS = 256 * 1_024
}

internal fun redactDiagnosticsLine(line: String): String {
    if (DiagnosticPatterns.sensitiveLine.containsMatchIn(line)) return "[redacted sensitive diagnostic line]"
    return line
        .replace(DiagnosticPatterns.networkUrl, "<url>")
        .replace(DiagnosticPatterns.contentUri, "<content-uri>")
        .replace(DiagnosticPatterns.userFilePath, "<file-path>")
        .replace(DiagnosticPatterns.ipv4Address, "<ip-address>")
        .replace(DiagnosticPatterns.uuidValue, "<identifier>")
}

private object DiagnosticPatterns {
    val sensitiveLine = Regex("(?i)(password|passwd|secret|authorization|credential|clipboard|access[_ -]?token|refresh[_ -]?token)")
    val networkUrl = Regex("(?i)\\b(?:https?|wss?)://[^\\s]+")
    val contentUri = Regex("(?i)content://[^\\s]+")
    val userFilePath = Regex("(?i)(?:[a-z]:\\\\|/(?:storage|sdcard|data/user|data/data)/)[^\\s]+")
    val ipv4Address = Regex("(?<![0-9])(?:[0-9]{1,3}\\.){3}[0-9]{1,3}(?![0-9])")
    val uuidValue = Regex("(?i)\\b[0-9a-f]{8}-[0-9a-f]{4}-[1-5][0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}\\b")
}
