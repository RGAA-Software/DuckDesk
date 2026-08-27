#include <gtest/gtest.h>

#include "px_common_new/http_client.h"
#include "px_console_client/console_api.h"

namespace {

TEST(ConsoleApiError, PrefersBusinessCodeOverHttpStatus) {
    px::HttpResponse response;
    response.status = 400;
    response.body = R"({"code":602,"message":"device not found","data":null})";

    EXPECT_EQ(px_console::ToConsoleApiError(response),
              px_console::ConsoleApiError::kDeviceNotFound);
    EXPECT_EQ(px_console::ConsoleApiLastErrorMessage(), "device not found");
}

TEST(ConsoleApiError, MalformedBadRequestDoesNotLookLikeMissingDevice) {
    px::HttpResponse response;
    response.status = 400;
    response.body = "not-json";

    const auto error = px_console::ToConsoleApiError(response);
    EXPECT_EQ(error, px_console::ConsoleApiError::kInternalError);
    EXPECT_NE(error, px_console::ConsoleApiError::kDeviceNotFound);
}

TEST(ConsoleApiError, MapsAuthorizationStatusWithoutJsonBody) {
    px::HttpResponse response;
    response.status = 401;

    EXPECT_EQ(px_console::ToConsoleApiError(response),
              px_console::ConsoleApiError::kInvalidAppkey);
}

TEST(ConsoleApiError, PreservesTransportFailureDetails) {
    px::HttpResponse response;
    response.status = 0;
    response.error_code = 7;
    response.error_message = "Failed to connect to 10.0.0.16 port 30500";

    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kNetworkUnavailable);
    EXPECT_EQ(px_console::ConsoleApiLastErrorMessage(), response.error_message);
}

TEST(ConsoleApiError, SuppliesDetailWhenTransportReturnsNoMessage) {
    px::HttpResponse response;
    response.status = 0;

    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kNetworkUnavailable);
    EXPECT_FALSE(px_console::ConsoleApiLastErrorMessage().empty());
}

TEST(ConsoleApiError, UserEndpointsKeepAuthenticationSemantics) {
    px::HttpResponse response;
    response.status = 401;
    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kAuthenticationRequired);

    response.status = 403;
    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kForbidden);
}

TEST(ConsoleApiError, MapsStandardUserHttpErrors) {
    px::HttpResponse response;
    response.status = 404;
    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kNotFound);

    response.status = 410;
    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kGone);

    response.status = 429;
    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kRateLimited);

    response.status = 503;
    EXPECT_EQ(px_console::ToConsoleUserApiError(response),
              px_console::ConsoleApiError::kServiceUnavailable);
}

} // namespace
