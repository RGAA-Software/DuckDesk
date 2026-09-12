#include "panel_product_runtime.h"

#include "px_common/md5.h"

#include <array>
#include <cstdint>
#include <limits>
#include <mutex>
#include <utility>

namespace px::panel::product {
namespace {

class ProductSecurityRecordsPort final : public ui::SecurityRecordsPort {
  public:
    explicit ProductSecurityRecordsPort(std::shared_ptr<PanelProductRuntime> runtime) : runtime_{std::move(runtime)} {}
    std::vector<ui::SecurityRecord> Snapshot(const ui::SecurityRecordKind kind) override {
        const std::size_t index{kind == ui::SecurityRecordKind::Visit ? 0U : 1U};
        const auto revision = runtime_->AuditStore()->Revision();
        const std::scoped_lock lock{mutex_};
        if (revisions_[index] != revision) {
            records_[index] = runtime_->AuditStore()->Query(kind);
            revisions_[index] = revision;
        }
        return records_[index];
    }
    bool Delete(const ui::SecurityRecordKind kind, const int id, const std::string& password) override {
        if (!Authorized(password))
            return false;
        return runtime_->AuditStore()->Delete(kind, id);
    }
    bool DeleteAll(const ui::SecurityRecordKind kind, const std::string& password) override {
        if (!Authorized(password))
            return false;
        return runtime_->AuditStore()->DeleteAll(kind);
    }

  private:
    bool Authorized(const std::string& password) const {
        const auto hash = runtime_->Config()->Identity().securityPasswordHash;
        const bool configured{!hash.empty()};
        return configured && MD5::Hex(password) == hash;
    }
    std::shared_ptr<PanelProductRuntime> runtime_{};
    std::mutex mutex_{};
    std::array<std::uint64_t, 2> revisions_{std::numeric_limits<std::uint64_t>::max(), std::numeric_limits<std::uint64_t>::max()};
    std::array<std::vector<ui::SecurityRecord>, 2> records_{};
};

} // namespace

std::shared_ptr<ui::SecurityRecordsPort> CreateProductSecurityRecordsPort(const std::shared_ptr<PanelProductRuntime>& runtime) {
    return std::make_shared<ProductSecurityRecordsPort>(runtime);
}

} // namespace px::panel::product
