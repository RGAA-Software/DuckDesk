#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "console_errors.h"
#include "px_common/expected.h"

namespace px {
class HttpResponse;
}

namespace px_console {

[[nodiscard]] ConsoleApiError ToConsoleUserApiError(const px::HttpResponse& response);

[[nodiscard]] px::Result<bool, ConsoleApiError> QueryConsoleReady(const std::string& host, int port,
                                                                  const std::shared_ptr<std::atomic_bool>& cancellation = {});

}  // namespace px_console
