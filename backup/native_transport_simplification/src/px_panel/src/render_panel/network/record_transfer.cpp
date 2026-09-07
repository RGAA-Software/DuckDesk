//
// Created by RGAA on 2026/08/17.
//

#include "record_transfer.h"

#include <chrono>

namespace px
{

    int64_t RecordFetchQueue::RetryDelayMs(int attempt) {
        if (attempt < 1) {
            attempt = 1;
        }
        // 2s, 4s, 8s, ... clamp the shift to avoid overflow on misuse
        const int shift = attempt - 1 > 10 ? 10 : attempt - 1;
        return kBaseBackoffMs * (int64_t(1) << shift);
    }

    bool RecordFetchQueue::Push(const RecordFetchTask& task) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopped_ || names_.count(task.filename) > 0) {
            return false;
        }
        names_.insert(task.filename);
        queue_.push_back(task);
        return true;
    }

    bool RecordFetchQueue::TryStartPump() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopped_ || pump_active_ || queue_.empty()) {
            return false;
        }
        pump_active_ = true;
        return true;
    }

    bool RecordFetchQueue::TryPop(RecordFetchTask& out) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopped_ || !pump_active_ || queue_.empty()) {
            return false;
        }
        out = queue_.front();
        queue_.pop_front();
        return true;
    }

    bool RecordFetchQueue::KeepPumpRunning() {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopped_) {
            pump_active_ = false;
            return false;
        }
        if (!queue_.empty()) {
            return true;
        }
        pump_active_ = false;
        return false;
    }

    void RecordFetchQueue::AbortPump() {
        std::lock_guard<std::mutex> lk(mtx_);
        pump_active_ = false;
    }

    bool RecordFetchQueue::Requeue(const RecordFetchTask& task) {
        std::lock_guard<std::mutex> lk(mtx_);
        if (stopped_) {
            return false;
        }
        queue_.push_back(task);
        return true;
    }

    void RecordFetchQueue::Finish(const std::string& filename) {
        std::lock_guard<std::mutex> lk(mtx_);
        names_.erase(filename);
    }

    void RecordFetchQueue::Stop() {
        std::lock_guard<std::mutex> lk(mtx_);
        stopped_ = true;
        pump_active_ = false;
        queue_.clear();
        names_.clear();
    }

    bool RecordFetchQueue::IsStopped() {
        std::lock_guard<std::mutex> lk(mtx_);
        return stopped_;
    }

    size_t RecordFetchQueue::Size() {
        std::lock_guard<std::mutex> lk(mtx_);
        return queue_.size();
    }

    bool ParseUploadUrl(const std::string& url, bool& ssl, std::string& host, int& port, std::string& path) {
        std::string rest;
        if (url.rfind("https://", 0) == 0) {
            ssl = true;
            rest = url.substr(8);
        } else if (url.rfind("http://", 0) == 0) {
            ssl = false;
            rest = url.substr(7);
        } else {
            return false;
        }

        const auto slash = rest.find('/');
        std::string authority = slash == std::string::npos ? rest : rest.substr(0, slash);
        path = slash == std::string::npos ? "/" : rest.substr(slash);
        if (authority.empty()) {
            return false;
        }

        const auto colon = authority.rfind(':');
        if (colon == std::string::npos) {
            host = authority;
            port = ssl ? 443 : 80;
        } else {
            host = authority.substr(0, colon);
            const std::string port_str = authority.substr(colon + 1);
            if (host.empty() || port_str.empty()) {
                return false;
            }
            try {
                port = std::stoi(port_str);
            } catch (...) {
                return false;
            }
            if (port <= 0 || port > 65535) {
                return false;
            }
        }
        return true;
    }

    int64_t FileMtimeSeconds(const std::filesystem::path& p) {
        std::error_code ec;
        const auto ft = std::filesystem::last_write_time(p, ec);
        if (ec) {
            return 0;
        }
        const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ft - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
        return std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();
    }

}
