#pragma once

#include "px_ui/widget_types.h"

#include <string_view>

namespace px::ui {

[[nodiscard]] bool ActionButton(WidgetId id, std::string_view label, const ButtonOptions& options = {});
[[nodiscard]] bool IconAction(WidgetId id, VectorIcon icon, std::string_view tooltip, const ButtonOptions& options = {});

} // namespace px::ui
