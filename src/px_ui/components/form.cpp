#include "px_ui/components/form.h"

#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <string>

namespace px::ui {
namespace {

std::string HiddenLabel(const std::string_view id) {
    std::string label{"##px-field-"};
    label.append(id);
    return label;
}

void BeginFieldStyle(const FieldOptions& options, const ThemeTokens& tokens, const UiMetrics& metrics) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, metrics.controlRadius);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{metrics.spacingMd, metrics.spacingSm * 0.75F});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, tokens.background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tokens.muted);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tokens.background);
    ImGui::PushStyleColor(ImGuiCol_Border, options.invalid ? tokens.destructive : tokens.input);
}

void EndFieldStyle() {
    ImGui::PopStyleColor(4);
    ImGui::PopStyleVar(2);
}

} // namespace

void FieldLabel(const std::string_view label) {
    ImGui::TextUnformatted(label.data(), label.data() + label.size());
}

void FieldDescription(const std::string_view description) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
    ImGui::TextWrapped("%.*s", static_cast<int>(description.size()), description.data());
    ImGui::PopStyleColor();
}

void FieldError(const std::string_view error) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    ImGui::PushStyleColor(ImGuiCol_Text, tokens.destructive);
    ImGui::TextWrapped("%.*s", static_cast<int>(error.size()), error.data());
    ImGui::PopStyleColor();
}

bool TextField(const WidgetId id, std::string& value, const std::string_view hint, const FieldOptions& options, const ImGuiInputTextFlags flags) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabled{options.disabled};
    const float width{options.width != 0.0F ? options.width : -1.0F};
    ImGui::SetNextItemWidth(width);
    BeginFieldStyle(options, tokens, metrics);
    const std::string label{HiddenLabel(id.value)};
    const std::string visibleHint{hint};
    const ImGuiInputTextFlags resolvedFlags{flags | (options.readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None)};
    const bool changed{ImGui::InputTextWithHint(label.c_str(), visibleHint.c_str(), &value, resolvedFlags)};
    EndFieldStyle();
    return changed;
}

bool TextArea(const WidgetId id, std::string& value, const ImVec2 size, const FieldOptions& options) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabled{options.disabled};
    BeginFieldStyle(options, tokens, metrics);
    const std::string label{HiddenLabel(id.value)};
    const bool changed{
        ImGui::InputTextMultiline(label.c_str(), &value, size, options.readOnly ? ImGuiInputTextFlags_ReadOnly : ImGuiInputTextFlags_None)};
    EndFieldStyle();
    return changed;
}

bool NumberField(const WidgetId id, int& value, const int step, const int fastStep, const FieldOptions& options) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabled{options.disabled};
    ImGui::SetNextItemWidth(options.width != 0.0F ? options.width : -1.0F);
    BeginFieldStyle(options, tokens, metrics);
    const std::string label{HiddenLabel(id.value)};
    const bool changed{ImGui::InputInt(label.c_str(), &value, step, fastStep)};
    EndFieldStyle();
    return changed;
}

bool SliderIntField(const WidgetId id, int& value, const int minimum, const int maximum, const float width, const bool disabled) {
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabledScope{disabled};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedStyleVar rounding{ImGuiStyleVar_FrameRounding, metrics.controlRadius};
    const ScopedStyleColor frame{ImGuiCol_FrameBg, tokens.secondary};
    const ScopedStyleColor hovered{ImGuiCol_FrameBgHovered, tokens.muted};
    const ScopedStyleColor active{ImGuiCol_FrameBgActive, tokens.muted};
    const ScopedStyleColor grab{ImGuiCol_SliderGrab, tokens.primary};
    const ScopedStyleColor grabActive{ImGuiCol_SliderGrabActive, tokens.ring};
    ImGui::SetNextItemWidth(width != 0.0F ? width : -1.0F);
    return ImGui::SliderInt("##slider", &value, minimum, maximum);
}

bool CheckboxField(const WidgetId id, const std::string_view label, bool& value, const bool disabled) {
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabledScope{disabled};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedStyleVar rounding{ImGuiStyleVar_FrameRounding, metrics.controlRadius * 0.55F};
    const ScopedStyleColor frame{ImGuiCol_FrameBg, tokens.background};
    const ScopedStyleColor hovered{ImGuiCol_FrameBgHovered, tokens.muted};
    const ScopedStyleColor active{ImGuiCol_FrameBgActive, tokens.primary};
    const ScopedStyleColor check{ImGuiCol_CheckMark, tokens.primaryForeground};
    const ScopedStyleColor border{ImGuiCol_Border, tokens.input};
    const std::string visible{label};
    return ImGui::Checkbox(visible.c_str(), &value);
}

bool ToggleSwitch(const WidgetId id, const std::string_view label, bool& value, const bool disabled) {
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabledScope{disabled};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 switchSize{metrics.controlDefault * 1.15F, metrics.controlDefault * 0.58F};
    const bool pressed{ImGui::InvisibleButton("##switch", switchSize)};
    if (pressed) {
        value = !value;
    }
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    const ImVec4 track{value ? tokens.primary : (ImGui::IsItemHovered() ? tokens.input : tokens.secondary)};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.AddRectFilled(minimum, maximum, ImGui::GetColorU32(track), switchSize.y * 0.5F);
    const float radius{switchSize.y * 0.5F - metrics.borderWidth * 2.0F};
    const float centerX{value ? maximum.x - switchSize.y * 0.5F : minimum.x + switchSize.y * 0.5F};
    draw.AddCircleFilled({centerX, minimum.y + switchSize.y * 0.5F}, radius, ImGui::GetColorU32(tokens.primaryForeground));
    if (!label.empty()) {
        ImGui::SameLine();
        const std::string visible{label};
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(visible.c_str());
        if (ImGui::IsItemClicked() && !disabled) {
            value = !value;
            return true;
        }
    }
    return pressed;
}

bool SelectField(const WidgetId id, int& value, const std::span<const SelectOption> options, const float width, const bool disabled) {
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabledScope{disabled};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    std::string_view preview{};
    for (const SelectOption& option : options) {
        if (option.value == value) {
            preview = option.label;
            break;
        }
    }
    ImGui::SetNextItemWidth(width != 0.0F ? width : -1.0F);
    const ScopedStyleVar rounding{ImGuiStyleVar_FrameRounding, metrics.controlRadius};
    const ScopedStyleColor frame{ImGuiCol_FrameBg, tokens.background};
    const ScopedStyleColor hovered{ImGuiCol_FrameBgHovered, tokens.muted};
    const ScopedStyleColor border{ImGuiCol_Border, tokens.input};
    const std::string label{HiddenLabel(id.value)};
    const std::string previewText{preview};
    bool changed{false};
    if (ImGui::BeginCombo(label.c_str(), previewText.c_str())) {
        for (const SelectOption& option : options) {
            const bool selected{option.value == value};
            const std::string optionText{option.label};
            if (ImGui::Selectable(optionText.c_str(), selected)) {
                value = option.value;
                changed = true;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    return changed;
}

} // namespace px::ui
