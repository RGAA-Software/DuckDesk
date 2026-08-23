//
// Created by RGAA on 2026/08/17.
//

#include "records_http_handler.h"
#include "records_catalog.h"
#include "records_ticket.h"
#include "render_panel/px_application.h"
#include "render_panel/px_settings.h"
#include "px_common_new/log.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/string_util.h"
#include "px_common_new/url_helper.h"
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>

namespace px
{

    namespace fs = std::filesystem;

    static const std::string kRecordsDirName = "px_render_records";
    static const std::string kRecordsFilePrefix = "/records/";

    std::string GetRenderRecordsDir() {
        return StringUtil::ToUTF8(FolderUtil::GetProgramDataPath()) + "/" + kRecordsDirName;
    }

    RecordsHttpHandler::RecordsHttpHandler(const std::shared_ptr<PxApplication>& app) {
        this->app_ = app;
    }

    // The console web page lives on the console origin and reads these endpoints via
    // cross-origin fetch (probe/list). Without ACAO the browser blocks the
    // response entirely and the page wrongly falls back to the console tunnel.
    void RecordsHttpHandler::SetCorsHeaders(http::web_response& rep) {
        rep.set(http::field::access_control_allow_origin, "*");
    }

    std::string RecordsHttpHandler::RecordsDir() const {
        return GetRenderRecordsDir();
    }

    bool RecordsHttpHandler::CheckTicket(http::web_request &req, http::web_response &rep,
                                         const std::string& filename_or_star) {
        auto settings = PxSettings::Instance();
        const std::string device_id = settings->GetDeviceId();
        // the stored safety password is already the md5 hex that console keeps as
        // safety_pwd_md5; it is used directly as the HMAC key (design 5.3).
        // do NOT hash it again here, or console-issued tickets will never match.
        const std::string ticket_key = settings->GetDeviceSecurityPwd();

        const auto query = req.query();
        auto params = UrlHelper::ParseQueryString(std::string(query.data(), query.size()));

        std::string tk;
        int64_t exp = 0;
        if (params.contains("tk")) {
            tk = params["tk"];
        }
        if (params.contains("exp")) {
            exp = std::strtoll(params["exp"].c_str(), nullptr, 10);
        }

        const int64_t now = static_cast<int64_t>(std::time(nullptr));
        if (!VerifyRecordsTicket(device_id, filename_or_star, exp, tk, ticket_key, now)) {
            LOGW("/records ticket rejected, file: {}", filename_or_star);
            rep.fill_text("forbidden", http::status::forbidden);
            return false;
        }
        return true;
    }

    void RecordsHttpHandler::HandleRecordsList(http::web_request &req, http::web_response &rep) {
        SetCorsHeaders(rep);
        if (!CheckTicket(req, rep, "*")) {
            return;
        }

        nlohmann::json files = nlohmann::json::array();
        for (const auto& info : ScanRecordFiles(fs::path(RecordsDir()))) {
            nlohmann::json item;
            item["name"] = info.name;
            item["size"] = info.size;
            item["mtime"] = info.mtime;
            item["monitor"] = info.monitor;
            item["codec"] = info.codec;
            files.push_back(std::move(item));
        }
        nlohmann::json obj;
        obj["files"] = std::move(files);
        obj["device_id"] = PxSettings::Instance()->GetDeviceId();
        rep.fill_json(obj.dump());
    }

    void RecordsHttpHandler::HandleRecordsInfo(http::web_request &req, http::web_response &rep) {
        SetCorsHeaders(rep);
        if (!CheckTicket(req, rep, "*")) {
            return;
        }

        const std::string dir = RecordsDir();
        nlohmann::json obj;
        obj["device_id"] = PxSettings::Instance()->GetDeviceId();
        obj["dir"] = dir;

        uint64_t total_bytes = 0;
        uint64_t free_bytes = 0;
        std::error_code ec;
        const auto si = fs::space(fs::path(dir), ec);
        if (!ec) {
            total_bytes = si.capacity;
            free_bytes = si.available;
        }
        obj["total_bytes"] = total_bytes;
        obj["free_bytes"] = free_bytes;
        obj["file_count"] = ScanRecordFiles(fs::path(dir)).size();
        rep.fill_json(obj.dump());
    }

    void RecordsHttpHandler::HandleRecordFile(http::web_request &req, http::web_response &rep) {
        SetCorsHeaders(rep);
        // extract + decode filename from "/records/{filename}"
        const auto path_sv = req.path();
        const std::string target = std::string(path_sv.data(), path_sv.size());
        if (target.rfind(kRecordsFilePrefix, 0) != 0) {
            rep.fill_text("bad request", http::status::bad_request);
            return;
        }
        const std::string filename = http::url_decode(target.substr(kRecordsFilePrefix.size()));

        if (!IsValidRecordFileName(filename)) {
            rep.fill_text("invalid file name", http::status::bad_request);
            return;
        }

        if (!CheckTicket(req, rep, filename)) {
            return;
        }

        const fs::path file_path = fs::path(RecordsDir()) / filename;
        std::error_code ec;
        if (!fs::exists(file_path, ec) || !fs::is_regular_file(file_path, ec)) {
            rep.fill_text("not found", http::status::not_found);
            return;
        }
        // still being recorded -> not playable yet
        if (HasRecordingSidecar(file_path)) {
            rep.fill_text("recording in progress", http::status::forbidden);
            return;
        }
        const uint64_t file_size = fs::file_size(file_path, ec);
        if (ec) {
            rep.fill_text("not found", http::status::not_found);
            return;
        }

        std::string range_header;
        if (auto it = req.find(http::field::range); it != req.end()) {
            range_header = std::string(it->value());
        }

        ByteRange range;
        const auto rr = ParseRangeHeader(range_header, file_size, range);
        if (rr == RangeParseResult::kInvalid || rr == RangeParseResult::kUnsatisfiable) {
            rep.fill_text("range not satisfiable", http::status::range_not_satisfiable);
            rep.set(http::field::content_range, std::format("bytes */{}", file_size));
            rep.set(http::field::accept_ranges, "bytes");
            return;
        }

        if (rr == RangeParseResult::kOk) {
            // serve one slice (<= 64MB) in memory
            std::ifstream ifs(file_path, std::ios::binary);
            if (!ifs) {
                rep.fill_text("read failed", http::status::internal_server_error);
                return;
            }
            const uint64_t len = range.end - range.begin + 1;
            std::string buf(static_cast<size_t>(len), '\0');
            ifs.seekg(static_cast<std::streamoff>(range.begin), std::ios::beg);
            ifs.read(buf.data(), static_cast<std::streamsize>(len));
            const auto got = static_cast<uint64_t>(ifs.gcount());
            if (got != len) {
                rep.fill_text("read failed", http::status::internal_server_error);
                return;
            }
            rep.fill_text(std::move(buf), http::status::partial_content, "video/mp4");
            rep.set(http::field::content_range, std::format("bytes {}-{}/{}", range.begin, range.end, file_size));
            rep.set(http::field::accept_ranges, "bytes");
            return;
        }

        // no Range header: stream the whole file via beast file body (no full memory load)
        // root_directory is cleared so fill_file accepts the absolute path
        rep.set_root_directory("");
        rep.fill_file(file_path);
        rep.set(http::field::content_type, "video/mp4");
        rep.set(http::field::accept_ranges, "bytes");
    }

}
