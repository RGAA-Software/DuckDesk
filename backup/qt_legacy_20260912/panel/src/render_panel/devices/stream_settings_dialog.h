#pragma once

#include <QCheckBox>
#include <QPointer>
#include <memory>
#include "px_console_client/console_stream.h"
#include "px_qt_widget/px_custom_titlebar_dialog.h"

namespace px {
class PxContext;
class StreamDBOperator;

class StreamSettingsDialog final : public TcCustomTitleBarDialog {
  public:
    StreamSettingsDialog(const std::shared_ptr<PxContext>& context, const std::shared_ptr<px_console::ConsoleStream>& item,
                         QPointer<QWidget> parent = {});

  private:
    void CreateLayout();
    void Save();
    std::shared_ptr<StreamDBOperator> database_{};
    std::shared_ptr<px_console::ConsoleStream> item_{};
    QPointer<QCheckBox> audio_{};
    QPointer<QCheckBox> clipboard_{};
    QPointer<QCheckBox> only_viewing_{};
    QPointer<QCheckBox> split_windows_{};
    QPointer<QCheckBox> software_{};
    QPointer<QCheckBox> tcp_{};
    QPointer<QCheckBox> relay_{};
    QPointer<QCheckBox> wait_debug_{};
    QPointer<QCheckBox> gdi_{};
    QPointer<QCheckBox> disable_vulkan_{};
};
} // namespace px
