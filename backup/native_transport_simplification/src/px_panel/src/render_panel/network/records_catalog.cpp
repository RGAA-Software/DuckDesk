//
// Created by RGAA on 2026/08/17.
//

#include "records_catalog.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <system_error>

namespace px
{

    namespace fs = std::filesystem;

    static bool EndsWith(const std::string& s, const std::string& suffix) {
        return s.size() >= suffix.size() && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    bool IsValidRecordFileName(const std::string& name) {
        // must have a non-empty stem before ".mp4"
        if (name.size() <= 4 || name.size() > 255) {
            return false;
        }
        if (!EndsWith(name, ".mp4")) {
            return false;
        }
        if (name.find("..") != std::string::npos) {
            return false;
        }
        for (char c : name) {
            const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '-';
            if (!ok) {
                return false;
            }
        }
        return true;
    }

    static bool AllDigits(const std::string& s, size_t from, size_t to) {
        if (to <= from) {
            return false;
        }
        for (size_t i = from; i < to; ++i) {
            if (!std::isdigit(static_cast<unsigned char>(s[i]))) {
                return false;
            }
        }
        return true;
    }

    bool ParseRecordFileName(const std::string& name, std::string& monitor, int64_t& epoch_seconds) {
        monitor.clear();
        epoch_seconds = 0;

        // strip ".mp4"
        std::string stem = name;
        if (EndsWith(stem, ".mp4")) {
            stem = stem.substr(0, stem.size() - 4);
        }
        // must start with "rec_"
        if (stem.rfind("rec_", 0) != 0) {
            return false;
        }

        // optional same-second collision suffix: ..._{HH.MM.SS}_N
        size_t tail_end = stem.size();
        const auto last_us = stem.rfind('_');
        if (last_us == std::string::npos) {
            return false;
        }
        // check if the last segment is a pure number AND the segment before it
        // looks like HH.MM.SS; otherwise the number belongs to the time part already
        // (time segment contains dots, a pure-digit last segment after '_' means _N suffix)
        {
            const auto prev_us = stem.rfind('_', last_us - 1);
            if (prev_us != std::string::npos &&
                AllDigits(stem, last_us + 1, stem.size()) &&
                stem.substr(prev_us + 1, last_us - prev_us - 1).find('.') != std::string::npos) {
                tail_end = last_us;
            }
        }

        // time segment: _HH.MM.SS (8 chars + underscore)
        if (tail_end < 9) {
            return false;
        }
        const std::string time_seg = stem.substr(tail_end - 8, 8);
        const size_t time_us = tail_end - 9;
        if (stem[time_us] != '_' ||
            !AllDigits(time_seg, 0, 2) || time_seg[2] != '.' ||
            !AllDigits(time_seg, 3, 5) || time_seg[5] != '.' ||
            !AllDigits(time_seg, 6, 8)) {
            return false;
        }

        // date segment: _YYYYMMDD (8 digits + underscore)
        if (time_us < 9) {
            return false;
        }
        const std::string date_seg = stem.substr(time_us - 8, 8);
        const size_t date_us = time_us - 9;
        if (stem[date_us] != '_' || !AllDigits(date_seg, 0, 8)) {
            return false;
        }

        // monitor name: between "rec_" and "_YYYYMMDD", may contain '_'
        if (date_us <= 4) {
            return false;
        }
        monitor = stem.substr(4, date_us - 4);
        if (monitor.empty()) {
            return false;
        }

        std::tm tm{};
        tm.tm_year = std::atoi(date_seg.substr(0, 4).c_str()) - 1900;
        tm.tm_mon = std::atoi(date_seg.substr(4, 2).c_str()) - 1;
        tm.tm_mday = std::atoi(date_seg.substr(6, 2).c_str());
        tm.tm_hour = std::atoi(time_seg.substr(0, 2).c_str());
        tm.tm_min = std::atoi(time_seg.substr(3, 2).c_str());
        tm.tm_sec = std::atoi(time_seg.substr(6, 2).c_str());
        tm.tm_isdst = -1;
        const std::time_t t = std::mktime(&tm); // local time, same as the recorder writes
        if (t == static_cast<std::time_t>(-1)) {
            return false;
        }
        epoch_seconds = static_cast<int64_t>(t);
        return true;
    }

    bool HasRecordingSidecar(const fs::path& file_path) {
        std::error_code ec;
        const fs::path sidecar = file_path.string() + ".recording";
        return fs::exists(sidecar, ec);
    }

    std::vector<RecordFileInfo> ScanRecordFiles(
        const fs::path& dir,
        const std::shared_ptr<std::atomic_bool>& cancellation_signal) {
        std::vector<RecordFileInfo> result;
        if (cancellation_signal
            && cancellation_signal->load(std::memory_order_acquire)) {
            return result;
        }
        std::error_code ec;
        if (!fs::exists(dir, ec) || !fs::is_directory(dir, ec)) {
            return result;
        }
        for (auto it = fs::directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
             !ec && it != fs::directory_iterator(); it.increment(ec)) {
            if (cancellation_signal
                && cancellation_signal->load(std::memory_order_acquire)) {
                return {};
            }
            const auto& entry = *it;
            if (!entry.is_regular_file(ec)) {
                continue;
            }
            const auto path = entry.path();
            const std::string fname = path.filename().string();
            // skip sidecar markers and non-mp4 files
            if (!EndsWith(fname, ".mp4")) {
                continue;
            }
            // skip files still being recorded
            if (HasRecordingSidecar(path)) {
                continue;
            }

            RecordFileInfo info;
            info.name = fname;
            info.size = entry.file_size(ec);
            if (ec) { ec.clear(); info.size = 0; }
            const auto ft = entry.last_write_time(ec);
            if (!ec) {
                const auto sys = std::chrono::clock_cast<std::chrono::system_clock>(ft);
                info.mtime = std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
            }
            else { ec.clear(); info.mtime = 0; }

            std::string monitor;
            int64_t ts = 0;
            if (ParseRecordFileName(fname, monitor, ts)) {
                info.monitor = monitor;
            }
            result.push_back(std::move(info));
        }
        // newest first
        std::sort(result.begin(), result.end(), [](const RecordFileInfo& a, const RecordFileInfo& b) {
            return a.mtime > b.mtime;
        });
        return result;
    }

    std::shared_ptr<RecordListRequestGate> RecordListRequestGate::Create(
        std::size_t maximum_outstanding) {
        return std::make_shared<RecordListRequestGate>(maximum_outstanding);
    }

    RecordListRequestGate::RecordListRequestGate(std::size_t maximum_outstanding)
        : maximum_outstanding_(std::max<std::size_t>(1, maximum_outstanding)) {}

    RecordListRequestAttempt RecordListRequestGate::TryStart() {
        std::lock_guard lock(mutex_);
        if (stopped_) {
            return {.result = RecordListRequestStartResult::kStopped};
        }
        if (active_.size() >= maximum_outstanding_) {
            return {.result = RecordListRequestStartResult::kLimitReached};
        }
        const auto sequence = next_sequence_++;
        const auto cancellation_signal = std::make_shared<std::atomic_bool>(false);
        active_.emplace(sequence, cancellation_signal);
        return {
            .result = RecordListRequestStartResult::kStarted,
            .sequence = sequence,
            .cancellation_signal = cancellation_signal,
        };
    }

    void RecordListRequestGate::Finish(std::uint64_t sequence) {
        std::lock_guard lock(mutex_);
        active_.erase(sequence);
    }

    void RecordListRequestGate::Stop() {
        std::vector<std::shared_ptr<std::atomic_bool>> cancellations;
        {
            std::lock_guard lock(mutex_);
            if (stopped_) {
                return;
            }
            stopped_ = true;
            cancellations.reserve(active_.size());
            for (const auto& [sequence, cancellation] : active_) {
                static_cast<void>(sequence);
                cancellations.push_back(cancellation);
            }
            active_.clear();
        }
        for (const auto& cancellation : cancellations) {
            cancellation->store(true, std::memory_order_release);
        }
    }

    std::size_t RecordListRequestGate::Outstanding() const {
        std::lock_guard lock(mutex_);
        return active_.size();
    }

    // ---- Range parsing ----

    static bool ParseUint64(const std::string& s, uint64_t& out) {
        if (s.empty() || s.size() > 20) {
            return false;
        }
        uint64_t v = 0;
        for (char c : s) {
            if (c < '0' || c > '9') {
                return false;
            }
            const uint64_t d = static_cast<uint64_t>(c - '0');
            if (v > (UINT64_MAX - d) / 10) {
                return false;
            }
            v = v * 10 + d;
        }
        out = v;
        return true;
    }

    RangeParseResult ParseRangeHeader(const std::string& header, uint64_t file_size, ByteRange& out) {
        if (header.empty()) {
            return RangeParseResult::kNone;
        }
        // unit is case-insensitive per RFC 9110; only "bytes" is supported
        std::string h = header;
        std::transform(h.begin(), h.end(), h.begin(), [](unsigned char c) { return std::tolower(c); });
        constexpr const char* kPrefix = "bytes=";
        if (h.rfind(kPrefix, 0) != 0) {
            return RangeParseResult::kInvalid;
        }
        const std::string spec = h.substr(6);
        // multi range is rejected (browsers only send single ranges)
        if (spec.find(',') != std::string::npos) {
            return RangeParseResult::kInvalid;
        }
        const auto dash = spec.find('-');
        if (dash == std::string::npos || dash != spec.rfind('-')) {
            return RangeParseResult::kInvalid;
        }
        const std::string first = spec.substr(0, dash);
        const std::string last = spec.substr(dash + 1);
        if (first.empty() && last.empty()) {
            return RangeParseResult::kInvalid;
        }
        if (file_size == 0) {
            return RangeParseResult::kUnsatisfiable;
        }

        uint64_t begin = 0;
        uint64_t end = file_size - 1;
        if (first.empty()) {
            // suffix range: bytes=-N -> last N bytes
            uint64_t suffix = 0;
            if (!ParseUint64(last, suffix)) {
                return RangeParseResult::kInvalid;
            }
            if (suffix == 0) {
                return RangeParseResult::kUnsatisfiable;
            }
            const uint64_t want = std::min<uint64_t>(suffix, file_size);
            begin = file_size - want;
        }
        else {
            if (!ParseUint64(first, begin)) {
                return RangeParseResult::kInvalid;
            }
            if (begin >= file_size) {
                return RangeParseResult::kUnsatisfiable;
            }
            if (!last.empty()) {
                if (!ParseUint64(last, end)) {
                    return RangeParseResult::kInvalid;
                }
                if (end < begin) {
                    return RangeParseResult::kUnsatisfiable;
                }
                end = std::min(end, file_size - 1);
            }
        }
        // clamp one slice to kMaxRangeSliceBytes
        if (end - begin + 1 > kMaxRangeSliceBytes) {
            end = begin + kMaxRangeSliceBytes - 1;
        }
        out.begin = begin;
        out.end = end;
        return RangeParseResult::kOk;
    }

}
