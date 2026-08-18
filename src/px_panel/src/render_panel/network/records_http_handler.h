//
// Created by RGAA on 2026/08/17.
//
// asio2 glue layer for the /records HTTP API
// (docs/cms_render_records_view_design.md section 5.1 / 5.3).
//

#ifndef TC_APPLICATION_RECORDS_HTTP_HANDLER_H
#define TC_APPLICATION_RECORDS_HTTP_HANDLER_H

#include <memory>
#include <string>
#include <asio2/asio2.hpp>

namespace px
{

    class PxApplication;

    // C:\Users\Public\Pixels\px_render_records (same convention as media_recorder plugin).
    // Shared by the /records http routes and the cms tunnel fetch worker.
    std::string GetRenderRecordsDir();

    class RecordsHttpHandler {
    public:

        explicit RecordsHttpHandler(const std::shared_ptr<PxApplication>& app);

        // GET /records       -> json file list
        void HandleRecordsList(http::web_request &req, http::web_response &rep);
        // GET /records/info  -> dir / space info (lightweight, for web topology probing)
        void HandleRecordsInfo(http::web_request &req, http::web_response &rep);
        // GET /records/{filename} -> file download, manual Range support
        void HandleRecordFile(http::web_request &req, http::web_response &rep);

    private:
        // ACAO:* so the cms web page (different origin) can fetch these endpoints
        static void SetCorsHeaders(http::web_response& rep);
        // verifies tk/exp query params, filename_or_star = "*" for list/info
        bool CheckTicket(http::web_request &req, http::web_response &rep, const std::string& filename_or_star);
        std::string RecordsDir() const;

    private:
        std::shared_ptr<PxApplication> app_ = nullptr;

    };

}

#endif //TC_APPLICATION_RECORDS_HTTP_HANDLER_H
