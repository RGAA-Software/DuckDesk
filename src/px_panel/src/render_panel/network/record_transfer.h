//
// Created by RGAA on 2026/08/17.
//
// Pure logic for the panel side of the record-fetch tunnel
// (docs/cms_render_records_view_design.md section 6.2 / 7.2):
// per-device serial upload queue with dedupe and exponential backoff retry,
// plus the upload-url parser. No Qt / asio2 / OpenSSL dependencies here so
// it can be unit tested standalone (see src/px_panel/src/tests/test_record_transfer.cpp).
//

#ifndef TC_APPLICATION_RECORD_TRANSFER_H
#define TC_APPLICATION_RECORD_TRANSFER_H

#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <mutex>
#include <set>
#include <string>

namespace px
{

    // One fetch task pushed by the cms RecordFetchReq tunnel message.
    struct RecordFetchTask {
        std::string device_id;
        std::string req_id;
        std::string filename;
        std::string token;
        std::string upload_url;
        int attempt = 0;    // completed (failed) attempts so far
    };

    // Serial queue: cms may push many fetches, panel uploads them one by one
    // (per panel == per device, panel is a singleton on the machine).
    class RecordFetchQueue {
    public:
        // total tries per task (1 initial + 2 retries)
        static constexpr int kMaxAttempts = 3;
        static constexpr int64_t kBaseBackoffMs = 2000;

        // Backoff before retry #attempt (attempt >= 1): 2s, 4s, ... (exponential).
        static int64_t RetryDelayMs(int attempt);

        // Returns false when a task for the same filename is already queued
        // or in-flight (dedupe; cms also dedupes, this is the second line).
        bool Push(const RecordFetchTask& task);

        // Blocks until a task is available. Returns false when stopped and drained.
        bool WaitPop(RecordFetchTask& out);

        // Puts a failed task back at the tail (dedupe set kept).
        void Requeue(const RecordFetchTask& task);

        // Marks the task finished (success or given up); allows the same
        // filename to be pushed again afterwards.
        void Finish(const std::string& filename);

        void Stop();
        bool IsStopped();
        size_t Size();

    private:
        std::mutex mtx_;
        std::condition_variable cv_;
        std::deque<RecordFetchTask> queue_;
        std::set<std::string> names_;       // queued + in-flight filenames
        bool stopped_ = false;
    };

    // Parse "http(s)://host[:port]/path" into parts.
    // port defaults to 80 (http) / 443 (https). Returns false on malformed input.
    bool ParseUploadUrl(const std::string& url, bool& ssl, std::string& host, int& port, std::string& path);

    // File mtime as unix seconds (0 on error).
    int64_t FileMtimeSeconds(const std::filesystem::path& p);

}

#endif //TC_APPLICATION_RECORD_TRANSFER_H
