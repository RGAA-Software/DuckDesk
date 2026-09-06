package yun.pixels.client.feature.settings

import androidx.annotation.RawRes
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext

@Composable
internal fun rememberOpenSourceNotices(): String {
    val resources = LocalContext.current.resources
    return remember(resources) {
        NOTICE_SECTIONS.joinToString(separator = "\n\n\n") { section ->
            val body = resources.openRawResource(section.resource).bufferedReader(Charsets.UTF_8).use { reader ->
                reader.readText().trim()
            }
            "${section.title}\n${"=".repeat(section.title.length)}\n\n$body"
        }
    }
}

private data class NoticeSection(val title: String, @RawRes val resource: Int)

private val NOTICE_SECTIONS = listOf(
    NoticeSection("Component inventory", R.raw.open_source_inventory),
    NoticeSection("Apache License 2.0", R.raw.license_apache_2_0),
    NoticeSection("WebRTC BSD 3-Clause notice", R.raw.license_webrtc_bsd_3_clause),
    NoticeSection("Protocol Buffers BSD 3-Clause notice", R.raw.license_protobuf_bsd_3_clause),
    NoticeSection("LevelDB BSD 3-Clause notice", R.raw.license_leveldb_bsd_3_clause),
    NoticeSection("Opus BSD 3-Clause and patent notices", R.raw.license_opus_bsd_3_clause),
    NoticeSection("Boost Software License 1.0", R.raw.license_boost_1_0),
    NoticeSection("GLM licenses", R.raw.license_glm),
    NoticeSection("zlib License", R.raw.license_zlib),
    NoticeSection("FFmpeg GNU LGPL 2.1-or-later", R.raw.license_ffmpeg_lgpl_2_1),
)
