//
// Created by RGAA on 2026/08/17.
//
// Pure logic for the /records HTTP API (console render records view, design doc
// docs/console_render_records_view_design.md section 5.1).
// No Qt / asio2 dependencies here so it can be unit tested standalone.
//

#ifndef TC_APPLICATION_RECORDS_CATALOG_H
#define TC_APPLICATION_RECORDS_CATALOG_H

#include <cstdint>
#include <string>
#include <vector>
#include <filesystem>

namespace px
{

    struct RecordFileInfo {
        std::string name;                       // file name only, e.g. rec_DISPLAY1_20260817_10.30.00.mp4
        uint64_t    size = 0;                   // bytes
        int64_t     mtime = 0;                  // unix seconds
        std::string monitor;                    // parsed from file name, may contain '_'
        std::string codec = "h264";             // fixed default, no ffprobe on panel side
    };

    // Whitelist check for download requests: ^[A-Za-z0-9_.\-]+$ , must end with ".mp4",
    // no path separators, no "..".
    bool IsValidRecordFileName(const std::string& name);

    // Parse rec_{monitor}_{YYYYMMDD}_{HH.MM.SS}[_N].mp4
    // The monitor name may contain '_' itself, so the date/time segments are parsed
    // from the tail of the name. Returns false when the name does not match the scheme.
    bool ParseRecordFileName(const std::string& name, std::string& monitor, int64_t& epoch_seconds);

    // A file currently being recorded has a sidecar marker "<file>.recording" next to it.
    bool HasRecordingSidecar(const std::filesystem::path& file_path);

    // Scan the records directory for finished .mp4 files.
    // Files with a .recording sidecar (still being written) and the sidecar files
    // themselves are filtered out.
    std::vector<RecordFileInfo> ScanRecordFiles(const std::filesystem::path& dir);

    // ---- HTTP Range (single range only) ----

    // Upper bound of one served slice; larger requests are clamped (legal per RFC 7233).
    constexpr uint64_t kMaxRangeSliceBytes = 64ull * 1024 * 1024;

    struct ByteRange {
        uint64_t begin = 0;                     // inclusive
        uint64_t end = 0;                       // inclusive
    };

    enum class RangeParseResult {
        kNone,                                  // no Range header -> serve whole file (200)
        kOk,                                    // valid single range -> 206
        kInvalid,                               // malformed / multi range -> 416
        kUnsatisfiable,                         // well formed but out of bounds -> 416
    };

    // Parse a Range header value like "bytes=0-99", "bytes=100-", "bytes=-500".
    // file_size is the total size of the target file.
    RangeParseResult ParseRangeHeader(const std::string& header, uint64_t file_size, ByteRange& out);

}

#endif //TC_APPLICATION_RECORDS_CATALOG_H
