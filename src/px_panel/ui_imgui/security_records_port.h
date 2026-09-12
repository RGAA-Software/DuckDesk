#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace px::panel::ui {

enum class SecurityRecordKind : std::uint8_t { Visit, FileTransfer };

struct SecurityRecord final {
    int id{};
    bool succeeded{false};
    std::string type{};
    std::string startedAt{};
    std::string endedAt{};
    std::string duration{};
    std::string visitor{};
    std::string target{};
    std::string direction{};
    std::string fileName{};
    std::string plainText{};
    std::string json{};
};

class SecurityRecordsPort {
  public:
    virtual ~SecurityRecordsPort() = default;
    virtual std::vector<SecurityRecord> Snapshot(SecurityRecordKind kind) = 0;
    virtual bool Delete(SecurityRecordKind kind, int id, const std::string& password) = 0;
    virtual bool DeleteAll(SecurityRecordKind kind, const std::string& password) = 0;
};

std::shared_ptr<SecurityRecordsPort> CreatePreviewSecurityRecordsPort();

} // namespace px::panel::ui
