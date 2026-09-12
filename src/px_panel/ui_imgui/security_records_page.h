#pragma once

#include "security_records_port.h"

#include "px_ui/localization.h"

#include <array>
#include <memory>

namespace px::panel::ui {

class SecurityRecordsPage final {
  public:
    explicit SecurityRecordsPage(std::shared_ptr<SecurityRecordsPort> port);
    void Draw(const px::ui::Localizer& localizer);

  private:
    void DrawRecords(const px::ui::Localizer& localizer);
    void DrawDeleteDialog(const px::ui::Localizer& localizer);

    std::shared_ptr<SecurityRecordsPort> port_{};
    SecurityRecordKind selected_{SecurityRecordKind::Visit};
    std::array<char, 256> password_{};
    int pendingDeleteId_{};
    bool deleteAll_{false};
    bool openDeleteDialog_{false};
    bool passwordRejected_{false};
};

} // namespace px::panel::ui
