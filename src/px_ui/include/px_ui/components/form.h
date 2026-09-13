#pragma once

#include "px_ui/widget_types.h"

#include <imgui.h>

#include <span>
#include <string>
#include <string_view>

namespace px::ui {

struct SelectOption final {
    int value{0};
    std::string_view label{};
};

void FieldLabel(std::string_view label);
void FieldDescription(std::string_view description);
void FieldError(std::string_view error);
[[nodiscard]] bool TextField(WidgetId id, std::string& value, std::string_view hint = {}, const FieldOptions& options = {},
                             ImGuiInputTextFlags flags = ImGuiInputTextFlags_None);
[[nodiscard]] bool TextArea(WidgetId id, std::string& value, ImVec2 size, const FieldOptions& options = {});
[[nodiscard]] bool NumberField(WidgetId id, int& value, int step = 1, int fastStep = 10, const FieldOptions& options = {});
[[nodiscard]] bool SliderIntField(WidgetId id, int& value, int minimum, int maximum, float width = 0.0F, bool disabled = false);
[[nodiscard]] bool CheckboxField(WidgetId id, std::string_view label, bool& value, bool disabled = false);
[[nodiscard]] bool ToggleSwitch(WidgetId id, std::string_view label, bool& value, bool disabled = false);
[[nodiscard]] bool SelectField(WidgetId id, int& value, std::span<const SelectOption> options, float width = 0.0F, bool disabled = false);

} // namespace px::ui
