#pragma once

#include <string>
#include <string_view>

#include "px_ui/widget_types.h"

namespace px::ui {

[[nodiscard]] std::string EllipsizedText(std::string_view text, float maximumWidth);
void ClippedText(std::string_view text, float width = 0.0F, float height = 0.0F);
void KeyValueRow(std::string_view label, std::string_view value, float labelWidth);
void EmptyState(VectorIcon icon, std::string_view title, std::string_view description);
void LoadingSpinner(WidgetId id, float radius = 8.0F);
void Progress(float fraction, float width = 0.0F);
[[nodiscard]] bool SelectableRow(WidgetId id, std::string_view label, bool selected, ImGuiSelectableFlags flags = ImGuiSelectableFlags_None,
                                 ImVec2 size = {});
[[nodiscard]] bool SelectableIconRow(WidgetId id, VectorIcon icon, std::string_view label, bool selected,
                                     ImGuiSelectableFlags flags = ImGuiSelectableFlags_None, ImVec2 size = {});

}  // namespace px::ui
