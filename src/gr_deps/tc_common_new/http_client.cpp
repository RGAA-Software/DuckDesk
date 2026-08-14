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

namespace tc
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
        // CMS servers use self-signed certificates; disable peer verification
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

    HttpResponse HttpClient::Download(const std::string& path, std::function<void(const std::string& body)>&& download_cbk) {
        LOGI("Download: {}", path.c_str());
        cpr::Url url{path};
        cpr::Session session;
        session.SetHeader(cpr::Header{{"Accept-Encoding", "gzip"}});
        session.SetUrl(url);
        session.SetVerifySsl(path.starts_with("https://"));
        session.SetTimeout(cpr::Timeout{5000});

        auto fn_cbk = [=](std::string_view data, intptr_t /*userdata*/) -> bool {
            LOGI("Download size: {}", data.size());
            auto cpy = std::string(data.data(), data.size());
            download_cbk(cpy);
            return true;
        };

        cpr::Response response = session.Download(cpr::WriteCallback{fn_cbk, 0});
        return ToHttpResponse(response);
    }

    void HttpClient::SetVerifySsl(bool verify_ssl) {
        verify_ssl_ = verify_ssl;
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
