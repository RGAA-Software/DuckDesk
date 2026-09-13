#pragma once

#include "px_ui/widget_types.h"

#include <string_view>

namespace px::ui {

[[nodiscard]] bool NavigationItem(WidgetId id, VectorIcon icon, std::string_view label, bool selected, float width, WidgetSize size = WidgetSize::Sm,
                                  float height = 0.0F, float leadingIconInset = 0.0F, bool showSelectionIndicator = true);
[[nodiscard]] bool TabItem(WidgetId id, std::string_view label, bool selected, float width = 0.0F);
[[nodiscard]] bool SegmentedItem(WidgetId id, std::string_view label, bool selected, float width = 0.0F, WidgetSize size = WidgetSize::Sm);

} // namespace px::ui
