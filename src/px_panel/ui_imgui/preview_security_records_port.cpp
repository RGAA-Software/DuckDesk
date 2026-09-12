#include "security_records_port.h"

namespace px::panel::ui {
namespace {

class PreviewSecurityRecordsPort final : public SecurityRecordsPort {
  public:
    std::vector<SecurityRecord> Snapshot(const SecurityRecordKind kind) override {
        if (kind == SecurityRecordKind::Visit) {
            return {{.id = 1,
                     .succeeded = true,
                     .type = "Desktop",
                     .startedAt = "2026-09-11 10:00",
                     .endedAt = "2026-09-11 10:05",
                     .duration = "00:05:00",
                     .visitor = "109022351",
                     .target = "node90",
                     .plainText = "Desktop 109022351 -> node90",
                     .json = R"({"type":"Desktop"})"}};
        }
        return {{.id = 2,
                 .succeeded = true,
                 .startedAt = "2026-09-11 10:02",
                 .endedAt = "2026-09-11 10:02",
                 .visitor = "109022351",
                 .target = "node90",
                 .direction = "upload",
                 .fileName = "sample.zip",
                 .plainText = "sample.zip",
                 .json = R"({"file":"sample.zip"})"}};
    }
    bool Delete(SecurityRecordKind, int, const std::string& password) override { return password == "preview"; }
    bool DeleteAll(SecurityRecordKind, const std::string& password) override { return password == "preview"; }
};

} // namespace

std::shared_ptr<SecurityRecordsPort> CreatePreviewSecurityRecordsPort() {
    return std::make_shared<PreviewSecurityRecordsPort>();
}

} // namespace px::panel::ui
