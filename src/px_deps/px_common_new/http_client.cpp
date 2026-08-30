#include "http_client.h"

#include "log.h"
#include <memory>
#include <string>
#include <cpr/error.h>
#include <cpr/cpr.h>
#include <cpr/cprtypes.h>
#include <cpr/redirect.h>
#include <cpr/session.h>

#include <asio2/asio2.hpp>

namespace px
{
    namespace {
        cpr::Header ToCprHeader(const std::map<std::string, std::string>& headers) {
            cpr::Header header;
            for (const auto& [k, v] : headers) {
                header.insert({k, v});
            }
            return header;
        }

        HttpResponse ToHttpResponse(const cpr::Response& response) {
            return HttpResponse{
                .status = (int)response.status_code,
                .body = response.text,
                .error_code = (int)response.error.code,
                .error_message = response.error.message,
            };
        }
    }

    std::shared_ptr<HttpClient> HttpClient::Make(const std::string& host, int port, const std::string& path, int timeout_ms) {
        return std::make_shared<HttpClient>(host, port, path, false, timeout_ms);
    }

    std::shared_ptr<HttpClient> HttpClient::MakeSSL(const std::string& host, int port, const std::string& path, int timeout_ms) {
        return std::make_shared<HttpClient>(host, port, path, true, timeout_ms);
    }

//    std::shared_ptr<HttpClient> HttpClient::MakeDownloadHttp(const std::string& url) {
//        auto remove_prefix_url = url.substr(7, url.size());
//        int separated_pos = remove_prefix_url.find('/');
//        auto host = remove_prefix_url.substr(0, separated_pos);
//        auto path = remove_prefix_url.substr(separated_pos, remove_prefix_url.size());
//        LOGI("download, host: {}, path: {}", host.c_str(), path.c_str());
//        return std::make_shared<HttpClient>(host, path, false);
//    }
//
//    std::shared_ptr<HttpClient> HttpClient::MakeDownloadHttps(const std::string& url) {
//        auto remove_prefix_url = url.substr(8, url.size());
//        int separated_pos = remove_prefix_url.find('/');
//        auto host = remove_prefix_url.substr(0, separated_pos);
//        auto path = remove_prefix_url.substr(separated_pos, remove_prefix_url.size());
//        return std::make_shared<HttpClient>(host, path, true);
//    }

    HttpClient::HttpClient(const std::string& host, int port, const std::string& path, bool ssl, int timeout_ms) {
        this->host_ = host;
        this->port_ = port;
        this->path = path;
        this->ssl_ = ssl;
        // Console servers use self-signed certificates; disable peer verification
        // so HTTPS requests don't fail on certificate validation.
        this->verify_ssl_ = false;
        this->timeout_ms_ = timeout_ms;
    }

    HttpClient::~HttpClient() {

    }

    HttpResponse HttpClient::Request() {
        std::map<std::string, std::string> params;
        return Request(params);
    }

    HttpResponse HttpClient::Request(const std::map<std::string, std::string>& query, const std::string& body) {
        cpr::Parameters params;
        for (const auto& [k, v] : query) {
            // params
            params.Add({k, v});
        }

        //req_path_ = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, query_path);
        auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
        cpr::Url url{url_path};
        cpr::Session session;
        session.SetUrl(url);
        session.SetBody(body);
        session.SetVerifySsl(verify_ssl_);
        session.SetTimeout(cpr::Timeout{this->timeout_ms_});
        if (cancellation_signal_) {
            session.SetCancellationParam(cancellation_signal_);
        }
        if (!headers_.empty()) {
            session.SetHeader(ToCprHeader(headers_));
        }
        session.SetParameters(params);

        cpr::Response response = session.Get();
        req_path_ = response.url.str();
        return ToHttpResponse(response);
    }

    HttpResponse HttpClient::Post() {
        std::map<std::string, std::string> params;
        return Post(params);
    }

    HttpResponse HttpClient::Post(const std::map<std::string, std::string>& query, const std::string& body, const std::string content_type) {
        cpr::Parameters params;
        for (const auto& [k, v] : query) {
            // params
            params.Add({k, v});
        }

        auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
        cpr::Url url{url_path};
        cpr::Session session;
        session.SetUrl(url);
        session.SetVerifySsl(verify_ssl_);
        session.SetBody(body);
        session.SetTimeout(cpr::Timeout{this->timeout_ms_});
        if (cancellation_signal_) {
            session.SetCancellationParam(cancellation_signal_);
        }
        auto headers = headers_;
        if (!content_type.empty()) {
            headers["Content-Type"] = content_type;
        }
        if (!headers.empty()) {
            session.SetHeader(ToCprHeader(headers));
        }
        session.SetParameters(params);

        cpr::Response response = session.Post();
        req_path_ = response.url.str();
        return ToHttpResponse(response);
    }

    HttpResponse HttpClient::Patch(const std::map<std::string, std::string>& query, const std::string& body, const std::string content_type) {
        cpr::Parameters params;
        for (const auto& [k, v] : query) {
            params.Add({k, v});
        }

        auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
        cpr::Session session;
        session.SetUrl(cpr::Url{url_path});
        session.SetVerifySsl(verify_ssl_);
        session.SetBody(body);
        session.SetTimeout(cpr::Timeout{this->timeout_ms_});
        if (cancellation_signal_) {
            session.SetCancellationParam(cancellation_signal_);
        }
        auto headers = headers_;
        if (!content_type.empty()) {
            headers["Content-Type"] = content_type;
        }
        if (!headers.empty()) {
            session.SetHeader(ToCprHeader(headers));
        }
        session.SetParameters(params);

        cpr::Response response = session.Patch();
        req_path_ = response.url.str();
        return ToHttpResponse(response);
    }

    //auto resp = client.PostMultiPart(
    //    // query 参数
    //    {
    //        {"uid", "123"},
    //        {"debug", "1"}
    //    },
    //
    //    // form 字段
    //    {
    //        {"type", "avatar"},
    //        {"desc", "hello multipart"}
    //    },
    //
    //    // file 字段
    //    {
    //        {"avatar", "C:/img/a.png"},
    //        {"cover",  "C:/img/b.jpg"}
    //    }
    //);
    HttpResponse HttpClient::PostMultiPart(const std::map<std::string, std::string>& query,
                                           const std::map<std::string, std::string>& form_parts,
                                           const std::map<std::string, std::string>& file_parts) {
        // 构造 URL
        auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);

        cpr::Url url{url_path};
        cpr::Session session;
        session.SetUrl(url);
        session.SetVerifySsl(verify_ssl_);
        session.SetTimeout(cpr::Timeout{timeout_ms_});
        if (cancellation_signal_) {
            session.SetCancellationParam(cancellation_signal_);
        }
        if (!headers_.empty()) {
            session.SetHeader(ToCprHeader(headers_));
        }

        // --- URL Query ---
        if (!query.empty()) {
            cpr::Parameters params;
            for (auto& [k, v] : query) {
                params.Add({k, v});
            }
            session.SetParameters(params);
        }

        // --- Multipart ---
        cpr::Multipart multipart{};

        // 添加表单字段
        for (auto& [k, v] : form_parts) {
            multipart.parts.emplace_back(k, v);  // text field
        }

        // 添加文件字段
        for (auto& [k, path] : file_parts) {
            multipart.parts.emplace_back(
                    k,
                    cpr::File{path}  // 自动推断 MIME
            );
        }

        // 设置 multipart
        session.SetMultipart(multipart);

        // POST
        cpr::Response response = session.Post();
        req_path_ = response.url.str();
        return ToHttpResponse(response);
    }

    HttpResponse HttpClient::PutMultiPart(const std::map<std::string, std::string>& query,
                                          const std::map<std::string, std::string>& form_parts,
                                          const std::map<std::string, std::string>& file_parts) {
        auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
        cpr::Session session;
        session.SetUrl(cpr::Url{url_path});
        session.SetVerifySsl(verify_ssl_);
        session.SetTimeout(cpr::Timeout{timeout_ms_});
        if (cancellation_signal_) {
            session.SetCancellationParam(cancellation_signal_);
        }
        if (!headers_.empty()) {
            session.SetHeader(ToCprHeader(headers_));
        }
        if (!query.empty()) {
            cpr::Parameters params;
            for (const auto& [k, v] : query) {
                params.Add({k, v});
            }
            session.SetParameters(params);
        }

        cpr::Multipart multipart{};
        for (const auto& [k, v] : form_parts) {
            multipart.parts.emplace_back(k, v);
        }
        for (const auto& [k, file_path] : file_parts) {
            multipart.parts.emplace_back(k, cpr::File{file_path});
        }
        session.SetMultipart(multipart);

        cpr::Response response = session.Put();
        req_path_ = response.url.str();
        return ToHttpResponse(response);
    }

    HttpResponse HttpClient::Download(const std::string& path, std::function<void(const std::string& body)>&& download_cbk) {
        HttpDownloadOptions options;
        options.verify_ssl = path.starts_with("https://");
        options.headers.emplace("Accept-Encoding", "gzip");
        options.write_callback = [callback = std::move(download_cbk)](std::string_view data) {
            callback(std::string(data));
            return true;
        };
        return Download(path, std::move(options));
    }

    HttpResponse HttpClient::Download(const std::string& path, HttpDownloadOptions options) {
        LOGI("Download: {}", path.c_str());
        cpr::Session session;
        session.SetUrl(cpr::Url{path});
        session.SetVerifySsl(options.verify_ssl);
        session.SetTimeout(cpr::Timeout{options.timeout_ms});
        if (!options.headers.empty()) {
            session.SetHeader(ToCprHeader(options.headers));
        }
        if (!options.query.empty()) {
            cpr::Parameters parameters;
            for (const auto& [key, value] : options.query) {
                parameters.Add({key, value});
            }
            session.SetParameters(parameters);
        }
        if (options.cancellation_signal) {
            session.SetCancellationParam(options.cancellation_signal);
        }
        if (options.progress_callback) {
            session.SetProgressCallback(cpr::ProgressCallback{
                [callback = std::move(options.progress_callback)](
                    cpr::cpr_pf_arg_t download_total,
                    cpr::cpr_pf_arg_t download_current,
                    cpr::cpr_pf_arg_t,
                    cpr::cpr_pf_arg_t,
                    intptr_t) {
                    const auto total = download_total > 0
                        ? static_cast<std::uint64_t>(download_total) : 0;
                    const auto current = download_current > 0
                        ? static_cast<std::uint64_t>(download_current) : 0;
                    return callback(total, current);
                }});
        }

        auto write_callback = std::move(options.write_callback);
        if (!write_callback) {
            write_callback = [](std::string_view) { return true; };
        }
        const auto response = session.Download(cpr::WriteCallback{
            [callback = std::move(write_callback)](
                const std::string_view& data, intptr_t) {
                return callback(data);
            }});

        return ToHttpResponse(response);
    }

    void HttpClient::SetVerifySsl(bool verify_ssl) {
        verify_ssl_ = verify_ssl;
    }

    void HttpClient::SetCancellationSignal(
        std::shared_ptr<std::atomic_bool> cancellation_signal) {
        cancellation_signal_ = std::move(cancellation_signal);
    }

    void HttpClient::SetHeader(const std::string& key, const std::string& value) {
        headers_[key] = value;
    }

    void HttpClient::ClearHeaders() {
        headers_.clear();
    }

    int HttpClient::HeadFileSize() {

        return 0;
    }

    std::string HttpClient::GetReqPath() {
        return req_path_;
    }

}
