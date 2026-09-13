#include "px_ui/components/form.h"

#include "px_ui/components/button.h"
#include "px_ui/style_scope.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <algorithm>
#include <limits>
#include <string>

namespace px::ui {
namespace {

std::string HiddenLabel(const std::string_view id) {
    std::string label{"##px-field-"};
    label.append(id);
    return label;
}

bool HasVisibleLabel(const std::string_view label) noexcept {
    return !label.empty() && !label.starts_with("##");
}

void BeginFieldStyle(const FieldOptions& options, const ThemeTokens& tokens, const UiMetrics& metrics) {
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, metrics.controlRadius);
    const float horizontalPadding{options.leadingIcon.has_value() ? metrics.spacingMd + metrics.iconDefault + metrics.spacingSm : metrics.spacingMd};
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{horizontalPadding, metrics.spacingSm * 0.75F});
    ImGui::PushStyleColor(ImGuiCol_FrameBg, tokens.background);
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, tokens.muted);
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive, tokens.background);
    ImGui::PushStyleColor(ImGuiCol_Border, options.invalid ? tokens.destructive : tokens.input);
}

void DrawFieldDecoration(const FieldOptions& options, const ThemeTokens& tokens, const UiMetrics& metrics) {
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    if (options.leadingIcon.has_value()) {
        DrawVectorIcon(*options.leadingIcon, {minimum.x + metrics.spacingMd, minimum.y + (maximum.y - minimum.y - metrics.iconDefault) * 0.5F},
                       metrics.iconDefault, ImGui::GetColorU32(tokens.mutedForeground));
    }
    if (ImGui::IsItemFocused()) {
        const float inset{metrics.borderWidth * 2.0F};
        const ImVec4 ringColor{options.invalid ? tokens.destructive : tokens.ring};
        draw.AddRect({minimum.x - inset, minimum.y - inset}, {maximum.x + inset, maximum.y + inset},
                     ImGui::GetColorU32({ringColor.x, ringColor.y, ringColor.z, 0.50F}), metrics.controlRadius + inset, ImDrawFlags_None,
                     metrics.borderWidth * 2.0F);
    }
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
    DrawFieldDecoration(options, tokens, metrics);
    return changed;
}

bool SearchField(const WidgetId id, std::string& value, const std::string_view hint, const FieldOptions& options) {
    FieldOptions resolved{options};
    resolved.leadingIcon = VectorIcon::Search;
    return TextField(id, value, hint, resolved);
}

bool PasswordField(const WidgetId id, std::string& value, const std::string_view hint, const FieldOptions& options) {
    const ScopedId scopedId{id.value};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const float actionSize{ImGui::GetFrameHeight()};
    const float actionGap{10.0F * metrics.scale};
    const float totalWidth{options.width > 0.0F ? options.width : ImGui::GetContentRegionAvail().x};
    FieldOptions fieldOptions{options};
    fieldOptions.width = std::max(metrics.controlLg * 2.0F, totalWidth - actionSize - actionGap);
    ImGuiStorage& storage{*ImGui::GetStateStorage()};
    const ImGuiID revealId{ImGui::GetID("password-revealed")};
    const bool revealed{storage.GetBool(revealId)};
    bool changed{TextField({"password-value"}, value, hint, fieldOptions, revealed ? ImGuiInputTextFlags_None : ImGuiInputTextFlags_Password)};
    ImGui::SameLine(0.0F, actionGap);
    if (IconAction({"password-visibility"}, revealed ? VectorIcon::EyeOff : VectorIcon::Eye, {},
                   {.variant = ButtonVariant::Outline, .size = WidgetSize::Icon, .height = actionSize})) {
        storage.SetBool(revealId, !revealed);
        changed = true;
    }
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
    DrawFieldDecoration(options, tokens, metrics);
    return changed;
}

bool NumberField(const WidgetId id, int& value, const int step, const int fastStep, const FieldOptions& options) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabled{options.disabled};
    const float buttonWidth{metrics.controlDefault};
    const float totalWidth{options.width > 0.0F ? options.width : ImGui::GetContentRegionAvail().x};
    const float fieldWidth{std::max(metrics.controlLg * 1.5F, totalWidth - buttonWidth * 2.0F)};
    ImGui::SetNextItemWidth(fieldWidth);
    BeginFieldStyle(options, tokens, metrics);
    const std::string label{HiddenLabel(id.value)};
    bool changed{ImGui::InputInt(label.c_str(), &value, 0, 0)};
    EndFieldStyle();
    DrawFieldDecoration(options, tokens, metrics);
    const int resolvedStep{ImGui::GetIO().KeyCtrl ? fastStep : step};
    const auto applyDelta = [&value, &changed, resolvedStep](const int direction) {
        const long long candidate{static_cast<long long>(value) + static_cast<long long>(resolvedStep) * direction};
        value = static_cast<int>(
            std::clamp(candidate, static_cast<long long>(std::numeric_limits<int>::min()), static_cast<long long>(std::numeric_limits<int>::max())));
        changed = true;
    };
    ImGui::SameLine(0.0F, 0.0F);
    if (IconAction({"number-decrease"}, VectorIcon::Minus, {}, {.variant = ButtonVariant::Outline, .size = WidgetSize::Icon})) {
        applyDelta(-1);
    }
    ImGui::SameLine(0.0F, 0.0F);
    if (IconAction({"number-increase"}, VectorIcon::Plus, {}, {.variant = ButtonVariant::Outline, .size = WidgetSize::Icon})) {
        applyDelta(1);
    }
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
    const float boxSize{metrics.iconLg};
    const bool hasLabel{HasVisibleLabel(label)};
    const ImVec2 textSize{hasLabel ? ImGui::CalcTextSize(label.data(), label.data() + label.size()) : ImVec2{}};
    const ImVec2 totalSize{boxSize + (hasLabel ? metrics.spacingSm + textSize.x : 0.0F), std::max(boxSize, textSize.y)};
    const bool pressed{ImGui::InvisibleButton("##checkbox", totalSize)};
    if (pressed) {
        value = !value;
    }
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{ImGui::GetItemRectMax()};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    const ImVec4 background{value ? tokens.primary : ImGui::IsItemHovered() ? tokens.muted : tokens.background};
    const ImVec4 border{value ? tokens.primary : tokens.input};
    const float top{minimum.y + (maximum.y - minimum.y - boxSize) * 0.5F};
    draw.AddRectFilled({minimum.x, top}, {minimum.x + boxSize, top + boxSize}, ImGui::GetColorU32(background), metrics.controlRadius * 0.55F);
    draw.AddRect({minimum.x, top}, {minimum.x + boxSize, top + boxSize}, ImGui::GetColorU32(border), metrics.controlRadius * 0.55F);
    if (value) {
        const float checkInset{metrics.borderWidth * 3.0F};
        DrawVectorIcon(VectorIcon::Check, {minimum.x + checkInset, top + checkInset}, boxSize - checkInset * 2.0F,
                       ImGui::GetColorU32(tokens.primaryForeground), metrics.borderWidth * 1.7F);
    }
    if (hasLabel) {
        const std::string visible{label};
        draw.AddText({minimum.x + boxSize + metrics.spacingSm, minimum.y + (maximum.y - minimum.y - textSize.y) * 0.5F},
                     ImGui::GetColorU32(tokens.foreground), visible.c_str());
    }
    if (ImGui::IsItemFocused()) {
        const float inset{metrics.borderWidth * 2.0F};
        draw.AddRect({minimum.x - inset, top - inset}, {minimum.x + boxSize + inset, top + boxSize + inset},
                     ImGui::GetColorU32({tokens.ring.x, tokens.ring.y, tokens.ring.z, 0.50F}), metrics.controlRadius, ImDrawFlags_None,
                     metrics.borderWidth * 2.0F);
    }
    return pressed;
}

bool ToggleSwitch(const WidgetId id, const std::string_view label, bool& value, const bool disabled, const std::optional<VectorIcon> icon) {
    const ScopedId scopedId{id.value};
    const ScopedDisabled disabledScope{disabled};
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec2 switchSize{metrics.controlDefault * 1.15F, metrics.controlDefault * 0.58F};
    const ImVec2 textSize{ImGui::CalcTextSize(label.data(), label.data() + label.size())};
    const float labelWidth{label.empty() ? 0.0F : metrics.spacingSm + textSize.x};
    const float iconWidth{icon.has_value() ? metrics.spacingSm + metrics.iconDefault : 0.0F};
    const ImVec2 totalSize{switchSize.x + iconWidth + labelWidth, std::max(switchSize.y, std::max(textSize.y, metrics.iconDefault))};
    const bool pressed{ImGui::InvisibleButton("##switch", totalSize)};
    if (pressed) {
        value = !value;
    }
    const ImVec2 minimum{ImGui::GetItemRectMin()};
    const ImVec2 maximum{minimum.x + switchSize.x, minimum.y + switchSize.y};
    const ImVec4 track{value ? tokens.primary : (ImGui::IsItemHovered() ? tokens.input : tokens.secondary)};
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    draw.AddRectFilled(minimum, maximum, ImGui::GetColorU32(track), switchSize.y * 0.5F);
    const float radius{switchSize.y * 0.5F - metrics.borderWidth * 2.0F};
    const float centerX{value ? maximum.x - switchSize.y * 0.5F : minimum.x + switchSize.y * 0.5F};
    draw.AddCircleFilled({centerX, minimum.y + switchSize.y * 0.5F}, radius, ImGui::GetColorU32(tokens.primaryForeground));
    float labelLeft{maximum.x};
    if (icon.has_value()) {
        labelLeft += metrics.spacingSm;
        DrawVectorIcon(*icon, {labelLeft, minimum.y + (switchSize.y - metrics.iconDefault) * 0.5F}, metrics.iconDefault,
                       ImGui::GetColorU32(tokens.mutedForeground));
        labelLeft += metrics.iconDefault;
    }
    if (!label.empty()) {
        const std::string visible{label};
        draw.AddText({labelLeft + metrics.spacingSm, minimum.y + (switchSize.y - textSize.y) * 0.5F}, ImGui::GetColorU32(tokens.foreground),
                     visible.c_str());
    }
    if (ImGui::IsItemFocused()) {
        const float inset{metrics.borderWidth * 2.0F};
        draw.AddRect({minimum.x - inset, minimum.y - inset}, {maximum.x + inset, maximum.y + inset},
                     ImGui::GetColorU32({tokens.ring.x, tokens.ring.y, tokens.ring.z, 0.50F}), switchSize.y * 0.5F + inset, ImDrawFlags_None,
                     metrics.borderWidth * 2.0F);
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
    const bool open{ImGui::BeginCombo(label.c_str(), previewText.c_str())};
    const ImVec2 fieldMinimum{ImGui::GetItemRectMin()};
    const ImVec2 fieldMaximum{ImGui::GetItemRectMax()};
    const bool fieldFocused{ImGui::IsItemFocused()};
    if (open) {
        for (const SelectOption& option : options) {
            const bool selected{option.value == value};
            const std::string optionText{option.label};
            if (ImGui::Selectable(optionText.c_str(), selected)) {
                value = option.value;
                changed = true;
            }
            if (selected) {
                const ImVec2 minimum{ImGui::GetItemRectMin()};
                const ImVec2 maximum{ImGui::GetItemRectMax()};
                DrawVectorIcon(VectorIcon::Check,
                               {maximum.x - metrics.spacingSm - metrics.iconSm, minimum.y + (maximum.y - minimum.y - metrics.iconSm) * 0.5F},
                               metrics.iconSm, ImGui::GetColorU32(tokens.primary));
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (fieldFocused) {
        const float inset{metrics.borderWidth * 2.0F};
        ImDrawList& draw{*ImGui::GetWindowDrawList()};
        draw.AddRect({fieldMinimum.x - inset, fieldMinimum.y - inset}, {fieldMaximum.x + inset, fieldMaximum.y + inset},
                     ImGui::GetColorU32({tokens.ring.x, tokens.ring.y, tokens.ring.z, 0.50F}), metrics.controlRadius + inset, ImDrawFlags_None,
                     metrics.borderWidth * 2.0F);
    }
    return changed;
}

} // namespace px::ui
