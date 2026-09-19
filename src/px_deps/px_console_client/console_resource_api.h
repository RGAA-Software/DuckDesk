#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "console_errors.h"
#include "px_common/expected.h"
#include "px_common/secret_buffer.h"

namespace px_console {

enum class ConsoleResourceTargetKind : std::uint8_t {
    Desktop,
    CloudApplication,
};

struct ConsoleResourceTarget final {
    ConsoleResourceTargetKind kind{ConsoleResourceTargetKind::Desktop};
    std::string device_id{};
    std::string application_id{};
    std::string instance_id{};
};

struct ConsoleResourceConnection final {
    std::string host{};
    int port{};
    std::string remote_resource_id{};
    std::string session_id{};
    std::int64_t session_revision{};
    std::shared_ptr<const px::SecretBuffer> frontend_token{};
    std::string transport{};
    std::string relay_host{};
    int relay_port{};
    std::string relay_admission_ticket{};
};

[[nodiscard]] px::Result<ConsoleResourceConnection, ConsoleApiError> OpenPanelResourceConnection(const std::string& host, int port,
                                                                                                 const std::string& access_token, bool guest,
                                                                                                 const ConsoleResourceTarget& target, bool view_only,
                                                                                                 const std::string& request_id);

[[nodiscard]] px::Result<bool, ConsoleApiError> ClosePanelResourceConnection(const std::string& host, int port, const std::string& access_token,
                                                                             bool guest, const std::string& session_id,
                                                                             std::int64_t session_revision);

}  // namespace px_console
