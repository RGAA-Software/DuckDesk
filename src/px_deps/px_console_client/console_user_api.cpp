#include "console_user_api.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string_view>

#include "console_api.h"
#include "console_http_client.h"
#include "px_common/http_client.h"
#include "px_common/log.h"

namespace px_console {
namespace {

using nlohmann::json;

constexpr std::string_view kAccountsPath{"/api/console/accounts"};
constexpr std::string_view kSessionsPath{"/api/console/sessions"};
constexpr std::string_view kSessionPath{"/api/console/session"};
constexpr std::string_view kProfilePath{"/api/console/profile"};
constexpr std::string_view kAvatarPath{"/api/console/profile/avatar"};
constexpr std::string_view kPasswordPath{"/api/console/password"};

template <typename Value>
px::Result<Value, ConsoleApiError> HttpError(const std::string_view operation, const px::HttpResponse& response) {
    const auto error = ToConsoleUserApiError(response);
    const auto message = ConsoleApiLastErrorMessage();
    LOGE("{} failed: HTTP {}, transport: {}, message: {}", operation, response.status, response.error_code, message.empty() ? "<empty>" : message);
    return TcErr(error);
}

px::Result<ConsoleUserPtr, ConsoleApiError> ParseUser(const std::string_view operation, const px::HttpResponse& response, const int expected_status) {
    if (response.status != expected_status || response.body.empty()) {
        return HttpError<ConsoleUserPtr>(operation, response);
    }
    try {
        auto user = ConsoleUser::FromObj(json::parse(response.body));
        return user ? px::Result<ConsoleUserPtr, ConsoleApiError>{std::move(user)} : TcErr(ConsoleApiError::kParseJsonFailed);
    } catch (const std::exception& error) {
        LOGE("{} response parsing failed: {}", operation, error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<ConsoleUserPtr, ConsoleApiError> QueryProfile(const std::string& host, const int port, const std::string& access_token) {
    const auto client = MakeConsoleHttpClient(host, port, std::string{kSessionPath});
    SetPanelRequestHeaders(client, access_token);
    return ParseUser("QueryProfile", client->Request(), 200);
}

std::string AvatarMediaType(const std::filesystem::path& path) {
    auto extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".webp") {
        return "image/webp";
    }
    return extension == ".jpg" || extension == ".jpeg" ? "image/jpeg" : std::string{};
}

}  // namespace

px::Result<ConsoleUserPtr, ConsoleApiError> ConsoleUserApi::Register(const std::string& host, const int port, const std::string& username,
                                                                     const std::string& password) {
    const auto client = MakeConsoleHttpClient(host, port, std::string{kAccountsPath});
    SetPanelRequestHeaders(client);
    const auto response = client->Post({}, json{{"username", username}, {"password", password}}.dump(), "application/json");
    return ParseUser("Register", response, 201);
}

px::Result<ConsoleUserLoginResult, ConsoleApiError> ConsoleUserApi::Login(const std::string& host, const int port, const std::string& username,
                                                                          const std::string& password) {
    const auto client = MakeConsoleHttpClient(host, port, std::string{kSessionsPath});
    SetPanelRequestHeaders(client);
    const auto response = client->Post({}, json{{"username", username}, {"password", password}}.dump(), "application/json");
    if (response.status != 200 || response.body.empty()) {
        return HttpError<ConsoleUserLoginResult>("Login", response);
    }
    try {
        const auto payload = json::parse(response.body);
        ConsoleUserLoginResult result{.user = ConsoleUser::FromObj(payload.at("profile")),
                                      .access_token = payload.value("token", ""),
                                      .expires_at = 0,
                                      .absolute_expires_at = 0};
        if (!result.user || result.access_token.empty()) {
            return TcErr(ConsoleApiError::kParseJsonFailed);
        }
        return result;
    } catch (const std::exception& error) {
        LOGE("Login response parsing failed: {}", error.what());
        return TcErr(ConsoleApiError::kParseJsonFailed);
    }
}

px::Result<bool, ConsoleApiError> ConsoleUserApi::Logout(const std::string& host, const int port, const std::string& access_token) {
    const auto client = MakeConsoleHttpClient(host, port, std::string{kSessionPath});
    SetPanelRequestHeaders(client, access_token);
    const auto response = client->Delete();
    return response.status == 204 ? px::Result<bool, ConsoleApiError>{true} : HttpError<bool>("Logout", response);
}

px::Result<ConsoleUserPtr, ConsoleApiError> ConsoleUserApi::UpdateProfile(const std::string& host, const int port, const std::string& access_token,
                                                                          const std::string& username) {
    const auto current = QueryProfile(host, port, access_token);
    if (!current) {
        return TcErr(current.error());
    }
    const auto client = MakeConsoleHttpClient(host, port, std::string{kProfilePath});
    SetPanelRequestHeaders(client, access_token);
    const auto response = client->Patch({}, json{{"username", username}, {"revision", current.value()->version_}}.dump(), "application/json");
    return ParseUser("UpdateProfile", response, 200);
}

px::Result<bool, ConsoleApiError> ConsoleUserApi::UpdatePassword(const std::string& host, const int port, const std::string& access_token,
                                                                 const std::string& old_password, const std::string& new_password) {
    const auto client = MakeConsoleHttpClient(host, port, std::string{kPasswordPath});
    SetPanelRequestHeaders(client, access_token);
    const auto response = client->Patch({}, json{{"current_password", old_password}, {"new_password", new_password}}.dump(), "application/json");
    return response.status == 204 ? px::Result<bool, ConsoleApiError>{true} : HttpError<bool>("UpdatePassword", response);
}

px::Result<ConsoleUserPtr, ConsoleApiError> ConsoleUserApi::UpdateAvatar(const std::string& host, const int port, const std::string& access_token,
                                                                         const std::string& avatar_path) {
    const auto current = QueryProfile(host, port, access_token);
    if (!current) {
        return TcErr(current.error());
    }
    const std::filesystem::path path{avatar_path};
    const auto media_type = AvatarMediaType(path);
    std::ifstream file{path, std::ios::binary};
    if (media_type.empty() || !file) {
        return TcErr(ConsoleApiError::kInvalidParams);
    }
    const std::string body{std::istreambuf_iterator<char>{file}, std::istreambuf_iterator<char>{}};
    const auto client = MakeConsoleHttpClient(host, port, std::string{kAvatarPath});
    SetPanelRequestHeaders(client, access_token);
    const auto response = client->Put({{"revision", std::to_string(current.value()->version_)}}, body, media_type);
    return ParseUser("UpdateAvatar", response, 200);
}

}  // namespace px_console
