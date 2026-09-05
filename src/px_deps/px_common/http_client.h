#pragma once 

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>

#include <cpr/error.h>
#include "cpr/cpr.h"
#include "cpr/cprtypes.h"
#include "cpr/redirect.h"
#include "cpr/session.h"

namespace px
{

    class HttpResponse {
    public:
        int status = -1;
        std::string body;
        int error_code = 0;
        std::string error_message;
    };

    struct HttpDownloadOptions {
        int timeout_ms = 5000;
        bool verify_ssl = false;
        std::map<std::string, std::string> query;
        std::map<std::string, std::string> headers;
        std::shared_ptr<std::atomic_bool> cancellation_signal;
        std::function<bool(std::string_view)> write_callback;
        std::function<bool(std::uint64_t total, std::uint64_t current)> progress_callback;
    };

    class HttpClient {
    public:

        static std::shared_ptr<HttpClient> Make(const std::string& host, int port, const std::string& path, int timeout_ms = 2000);
        static std::shared_ptr<HttpClient> MakeSSL(const std::string& host, int port, const std::string& path, int timeout_ms = 2000);
        //static std::shared_ptr<HttpClient> MakeDownloadHttp(const std::string& url);
        //static std::shared_ptr<HttpClient> MakeDownloadHttps(const std::string& url);

        HttpClient(const std::string& host, int port, const std::string& path, bool ssl, int timeout_ms = 2000);
        ~HttpClient();

        HttpResponse Request();
        HttpResponse Request(const std::map<std::string, std::string>& query, const std::string& body = "");
        HttpResponse Post();
        HttpResponse Post(const std::map<std::string, std::string>& query, const std::string& body = "", const std::string content_type = "");
        HttpResponse Patch(const std::map<std::string, std::string>& query, const std::string& body = "", const std::string content_type = "");
        HttpResponse PostMultiPart(const std::map<std::string, std::string>& query,
                                   const std::map<std::string, std::string>& form_parts,
                                   const std::map<std::string, std::string>& file_parts);
        HttpResponse PutMultiPart(const std::map<std::string, std::string>& query,
                                  const std::map<std::string, std::string>& form_parts,
                                  const std::map<std::string, std::string>& file_parts);

        static HttpResponse Download(const std::string& url, std::function<void(const std::string& body)>&& download_cbk);
        static HttpResponse Download(const std::string& url, HttpDownloadOptions options);
        void SetVerifySsl(bool verify_ssl);
        void SetCancellationSignal(std::shared_ptr<std::atomic_bool> cancellation_signal);
        void SetHeader(const std::string& key, const std::string& value);
        void ClearHeaders();
        
        std::string GetReqPath();

    private:
        std::string host_;
        int port_ = 0;
        std::string path;
        bool ssl_ = false;
        bool verify_ssl_ = false;
        int timeout_ms_ = 3000;
        std::string req_path_;
        std::map<std::string, std::string> headers_;
        std::shared_ptr<std::atomic_bool> cancellation_signal_;

    };

}
