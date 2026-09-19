#include "http_client.h"

#include <cpr/cpr.h>
#include <cpr/cprtypes.h>
#include <cpr/error.h>
#include <cpr/redirect.h>
#include <cpr/session.h>

#include <algorithm>
#include <asio2/asio2.hpp>
#include <filesystem>
#include <memory>
#include <string>
#include <utility>

#include "log.h"

namespace px {
namespace {
bool ConfigurePrivateCa(cpr::Session& session, const std::string& ca_file) {
    if (ca_file.empty()) {
        return true;
    }
    session.SetSslOptions(cpr::Ssl(cpr::ssl::CaInfo{std::filesystem::path{ca_file}}));
#ifdef _WIN32
    // Private deployment CAs may have no CRL distribution point. Keep
    // chain/name/revoked-certificate validation; tolerate only unavailable
    // revocation information. Do not enable NO_REVOKE or add system CAs.
    const auto holder = session.GetCurlHolder();
    return holder && curl_easy_setopt(holder->handle, CURLOPT_SSL_OPTIONS, static_cast<long>(CURLSSLOPT_REVOKE_BEST_EFFORT)) == CURLE_OK;
#else
    return true;
#endif
}

HttpResponse PrivateCaConfigurationError() {
    return {.status = 0,
            .body = {},
            .error_code = static_cast<int>(cpr::ErrorCode::SSL_CONNECT_ERROR),
            .error_message = "Private CA TLS configuration failed"};
}

cpr::Header ToCprHeader(const std::map<std::string, std::string>& headers) {
    cpr::Header header;
    for (const auto& [header_name, header_value] : headers) {
        header.insert({header_name, header_value});
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

void ConfigureBoundedResponse(cpr::Session& session, std::string& response_body, const std::size_t response_body_limit,
                              bool& response_body_limit_exceeded) {
    session.SetWriteCallback(
        cpr::WriteCallback{[&response_body, response_body_limit, &response_body_limit_exceeded](const std::string_view& chunk, intptr_t) {
            if (chunk.size() > response_body_limit - response_body.size()) {
                response_body_limit_exceeded = true;
                return false;
            }
            response_body.append(chunk);
            return true;
        }});
}

HttpResponse ToBoundedHttpResponse(cpr::Response response, std::string response_body, const bool response_body_limit_exceeded) {
    if (response_body_limit_exceeded) {
        return {.status = 0, .body = {}, .error_code = 23, .error_message = "HTTP response body limit exceeded"};
    }
    response.text = std::move(response_body);
    return ToHttpResponse(response);
}
}  // namespace

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
    this->verify_ssl_ = ssl;
    this->timeout_ms_ = timeout_ms;
}

HttpClient::~HttpClient() {}

HttpResponse HttpClient::Request() {
    std::map<std::string, std::string> params;
    return Request(params);
}

HttpResponse HttpClient::Request(const std::map<std::string, std::string>& query, const std::string& body) {
    cpr::Parameters params;
    for (const auto& [parameter_name, parameter_value] : query) {
        // params
        params.Add({parameter_name, parameter_value});
    }

    // req_path_ = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, query_path);
    auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
    cpr::Url url{url_path};
    cpr::Session session;
    session.SetUrl(url);
    session.SetRedirect(cpr::Redirect{false});
    session.SetBody(body);
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
    session.SetTimeout(cpr::Timeout{this->timeout_ms_});
    if (cancellation_signal_) {
        session.SetCancellationParam(cancellation_signal_);
    }
    if (!headers_.empty()) {
        session.SetHeader(ToCprHeader(headers_));
    }
    session.SetParameters(params);
    std::string responseBody{};
    bool responseBodyLimitExceeded{};
    ConfigureBoundedResponse(session, responseBody, response_body_limit_, responseBodyLimitExceeded);
    auto response = session.Get();
    req_path_ = response.url.str();
    return ToBoundedHttpResponse(std::move(response), std::move(responseBody), responseBodyLimitExceeded);
}

HttpResponse HttpClient::Post() {
    std::map<std::string, std::string> params;
    return Post(params);
}

HttpResponse HttpClient::Post(const std::map<std::string, std::string>& query, const std::string& body, const std::string content_type) {
    cpr::Parameters params;
    for (const auto& [parameter_name, parameter_value] : query) {
        // params
        params.Add({parameter_name, parameter_value});
    }

    auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
    cpr::Url url{url_path};
    cpr::Session session;
    session.SetUrl(url);
    session.SetRedirect(cpr::Redirect{false});
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
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
    std::string responseBody{};
    bool responseBodyLimitExceeded{};
    ConfigureBoundedResponse(session, responseBody, response_body_limit_, responseBodyLimitExceeded);
    auto response = session.Post();
    req_path_ = response.url.str();
    return ToBoundedHttpResponse(std::move(response), std::move(responseBody), responseBodyLimitExceeded);
}

HttpResponse HttpClient::Patch(const std::map<std::string, std::string>& query, const std::string& body, const std::string content_type) {
    cpr::Parameters params;
    for (const auto& [parameter_name, parameter_value] : query) {
        params.Add({parameter_name, parameter_value});
    }

    auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
    cpr::Session session;
    session.SetUrl(cpr::Url{url_path});
    session.SetRedirect(cpr::Redirect{false});
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
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

HttpResponse HttpClient::Put(const std::map<std::string, std::string>& query, const std::string& body, const std::string content_type) {
    cpr::Parameters params;
    for (const auto& [key, value] : query) {
        params.Add({key, value});
    }

    const auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
    cpr::Session session;
    session.SetUrl(cpr::Url{url_path});
    session.SetRedirect(cpr::Redirect{false});
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
    session.SetBody(body);
    session.SetTimeout(cpr::Timeout{timeout_ms_});
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

    const auto response = session.Put();
    req_path_ = response.url.str();
    return ToHttpResponse(response);
}

HttpResponse HttpClient::Delete(const std::map<std::string, std::string>& query) {
    cpr::Parameters params;
    for (const auto& [key, value] : query) {
        params.Add({key, value});
    }

    const auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
    cpr::Session session;
    session.SetUrl(cpr::Url{url_path});
    session.SetRedirect(cpr::Redirect{false});
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
    session.SetTimeout(cpr::Timeout{timeout_ms_});
    if (cancellation_signal_) {
        session.SetCancellationParam(cancellation_signal_);
    }
    if (!headers_.empty()) {
        session.SetHeader(ToCprHeader(headers_));
    }
    session.SetParameters(params);

    const auto response = session.Delete();
    req_path_ = response.url.str();
    return ToHttpResponse(response);
}

// auto resp = client.PostMultiPart(
//     // query 参数
//     {
//         {"uid", "123"},
//         {"debug", "1"}
//     },
//
//     // form 字段
//     {
//         {"type", "avatar"},
//         {"desc", "hello multipart"}
//     },
//
//     // file 字段
//     {
//         {"avatar", "C:/img/a.png"},
//         {"cover",  "C:/img/b.jpg"}
//     }
//);
HttpResponse HttpClient::PostMultiPart(const std::map<std::string, std::string>& query, const std::map<std::string, std::string>& form_parts,
                                       const std::map<std::string, std::string>& file_parts) {
    // 构造 URL
    auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);

    cpr::Url url{url_path};
    cpr::Session session;
    session.SetUrl(url);
    session.SetRedirect(cpr::Redirect{false});
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
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
        for (const auto& [parameter_name, parameter_value] : query) {
            params.Add({parameter_name, parameter_value});
        }
        session.SetParameters(params);
    }

    // --- Multipart ---
    cpr::Multipart multipart{};

    // 添加表单字段
    for (const auto& [field_name, field_value] : form_parts) {
        multipart.parts.emplace_back(field_name, field_value);  // text field
    }

    // 添加文件字段
    for (const auto& [field_name, file_path] : file_parts) {
        multipart.parts.emplace_back(field_name, cpr::File{file_path}  // 自动推断 MIME
        );
    }

    // 设置 multipart
    session.SetMultipart(multipart);

    // POST
    cpr::Response response = session.Post();
    req_path_ = response.url.str();
    return ToHttpResponse(response);
}

HttpResponse HttpClient::PutMultiPart(const std::map<std::string, std::string>& query, const std::map<std::string, std::string>& form_parts,
                                      const std::map<std::string, std::string>& file_parts) {
    auto url_path = std::format("{}{}:{}{}", ssl_ ? "https://" : "http://", host_, port_, path);
    cpr::Session session;
    session.SetUrl(cpr::Url{url_path});
    session.SetRedirect(cpr::Redirect{false});
    session.SetVerifySsl(verify_ssl_);
    if (!ConfigurePrivateCa(session, trusted_ca_file_)) {
        return PrivateCaConfigurationError();
    }
    session.SetTimeout(cpr::Timeout{timeout_ms_});
    if (cancellation_signal_) {
        session.SetCancellationParam(cancellation_signal_);
    }
    if (!headers_.empty()) {
        session.SetHeader(ToCprHeader(headers_));
    }
    if (!query.empty()) {
        cpr::Parameters params;
        for (const auto& [parameter_name, parameter_value] : query) {
            params.Add({parameter_name, parameter_value});
        }
        session.SetParameters(params);
    }

    cpr::Multipart multipart{};
    for (const auto& [field_name, field_value] : form_parts) {
        multipart.parts.emplace_back(field_name, field_value);
    }
    for (const auto& [field_name, file_path] : file_parts) {
        multipart.parts.emplace_back(field_name, cpr::File{file_path});
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
    options.write_callback = [callback = std::move(download_cbk)](std::string_view chunk) {
        callback(std::string(chunk));
        return true;
    };
    return Download(path, std::move(options));
}

HttpResponse HttpClient::Download(const std::string& path, HttpDownloadOptions options) {
    LOGI("Download: {}", path.c_str());
    cpr::Session session;
    session.SetUrl(cpr::Url{path});
    session.SetRedirect(cpr::Redirect{false});
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
            [callback = std::move(options.progress_callback)](cpr::cpr_pf_arg_t download_total, cpr::cpr_pf_arg_t download_current, cpr::cpr_pf_arg_t,
                                                              cpr::cpr_pf_arg_t, intptr_t) {
                const auto total = download_total > 0 ? static_cast<std::uint64_t>(download_total) : 0;
                const auto current = download_current > 0 ? static_cast<std::uint64_t>(download_current) : 0;
                return callback(total, current);
            }});
    }

    auto write_callback = std::move(options.write_callback);
    if (!write_callback) {
        write_callback = [](std::string_view) { return true; };
    }
    const auto response = session.Download(
        cpr::WriteCallback{[callback = std::move(write_callback)](const std::string_view& chunk, intptr_t) { return callback(chunk); }});

    return ToHttpResponse(response);
}

void HttpClient::SetVerifySsl(bool verify_ssl) { verify_ssl_ = verify_ssl; }

void HttpClient::SetTrustedCaFile(std::string path) {
    trusted_ca_file_ = std::move(path);
    verify_ssl_ = true;
}

void HttpClient::SetResponseBodyLimit(const std::size_t response_body_limit) { response_body_limit_ = std::max<std::size_t>(1, response_body_limit); }

void HttpClient::SetCancellationSignal(std::shared_ptr<std::atomic_bool> cancellation_signal) {
    cancellation_signal_ = std::move(cancellation_signal);
}

void HttpClient::SetHeader(const std::string& key, const std::string& value) { headers_[key] = value; }

void HttpClient::ClearHeaders() { headers_.clear(); }

std::string HttpClient::GetReqPath() { return req_path_; }

}  // namespace px
