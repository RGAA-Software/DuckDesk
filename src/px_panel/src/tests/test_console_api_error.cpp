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

} // namespace
