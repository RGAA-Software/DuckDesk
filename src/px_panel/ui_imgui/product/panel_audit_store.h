#pragma once

#include "security_records_port.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

struct sqlite3;

namespace pxrp {
class RpMessage;
}

namespace px::panel::product {

class PanelAuditStore final {
  public:
    static std::shared_ptr<PanelAuditStore> Create(const std::filesystem::path& dataDirectory);
    explicit PanelAuditStore(const std::filesystem::path& databasePath);
    ~PanelAuditStore();

    PanelAuditStore(const PanelAuditStore&) = delete;
    PanelAuditStore& operator=(const PanelAuditStore&) = delete;

    [[nodiscard]] bool Ready() const;
    void Consume(const pxrp::RpMessage& message, const std::string& targetDeviceId);
    [[nodiscard]] std::vector<ui::SecurityRecord> Query(ui::SecurityRecordKind kind) const;
    [[nodiscard]] std::uint64_t Revision() const;
    bool Delete(ui::SecurityRecordKind kind, int id);
    bool DeleteAll(ui::SecurityRecordKind kind);

  private:
    struct DatabaseDeleter final {
        void operator()(sqlite3* database) const noexcept; // NOLINT(gammaray-raw-pointer-boundary): SQLite ownership adapter
    };

    std::unique_ptr<sqlite3, DatabaseDeleter> database_{};
    mutable std::mutex mutex_{};
    std::atomic_uint64_t revision_{};
};

} // namespace px::panel::product
