#pragma once

#include <string_view>

#include "px_ui/components/form.h"
#include "px_ui/widget_types.h"

namespace px::ui {

[[nodiscard]] bool NavigationItem(WidgetId id, VectorIcon icon, std::string_view label, bool selected, float width, WidgetSize size = WidgetSize::Sm,
                                  float height = 0.0F, float leadingIconInset = 0.0F, bool showSelectionIndicator = true);
[[nodiscard]] bool TabItem(WidgetId id, std::string_view label, bool selected, float width = 0.0F);
[[nodiscard]] bool SegmentedItem(WidgetId id, std::string_view label, bool selected, float width = 0.0F, WidgetSize size = WidgetSize::Default);
[[nodiscard]] bool SegmentedControl(WidgetId id, int& value, std::span<const SelectOption> options);

}  // namespace px::ui
