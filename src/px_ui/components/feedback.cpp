#include "px_ui/components/feedback.h"

#include "px_ui/components/button.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <utility>

namespace px::ui {
namespace {

ImVec4 AccentFor(const FeedbackVariant variant, const ThemeTokens& tokens) noexcept {
    switch (variant) {
    case FeedbackVariant::Success:
        return tokens.success;
    case FeedbackVariant::Warning:
        return tokens.warning;
    case FeedbackVariant::Error:
        return tokens.destructive;
    case FeedbackVariant::Info:
    default:
        return tokens.primary;
    }
}

} // namespace

void ToastHost::Push(ToastMessage message) {
    const auto now{std::chrono::steady_clock::now()};
    const auto expiresAt{now + message.duration};
    entries_.push_back(Entry{.id = nextId_++, .message = std::move(message), .expiresAt = expiresAt});
    constexpr std::size_t maximumEntries{5};
    while (entries_.size() > maximumEntries) {
        entries_.pop_front();
    }
}

void ToastHost::Draw() {
    const auto now{std::chrono::steady_clock::now()};
    std::erase_if(entries_, [now](const Entry& entry) { return now >= entry.expiresAt; });
    if (entries_.empty()) {
        return;
    }
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImGuiViewport& viewport{*ImGui::GetMainViewport()};
    float bottom{viewport.WorkPos.y + viewport.WorkSize.y - metrics.spacingLg};
    for (auto iterator{entries_.rbegin()}; iterator != entries_.rend(); ++iterator) {
        Entry& entry{*iterator};
        const float height{entry.message.description.empty() ? metrics.controlLg + metrics.spacingMd : metrics.controlLg + metrics.spacingXl};
        const float width{320.0F * metrics.scale};
        bottom -= height;
        ImGui::SetNextWindowPos({viewport.WorkPos.x + viewport.WorkSize.x - width - metrics.spacingLg, bottom});
        ImGui::SetNextWindowSize({width, height});
        ImGui::PushStyleColor(ImGuiCol_WindowBg, tokens.popover);
        ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.popupRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingLg, metrics.spacingMd});
        const std::string windowId{"##px-toast-" + std::to_string(entry.id)};
        if (ImGui::Begin(windowId.c_str(), {}, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove |
                                                   ImGuiWindowFlags_NoNavFocus)) {
            const ImVec4 accent{AccentFor(entry.message.variant, tokens)};
            ImGui::PushStyleColor(ImGuiCol_Text, accent);
            ImGui::TextUnformatted(entry.message.title.c_str());
            ImGui::PopStyleColor();
            if (!entry.message.description.empty()) {
                ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
                ImGui::TextWrapped("%s", entry.message.description.c_str());
                ImGui::PopStyleColor();
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        bottom -= metrics.spacingSm;
    }
}

void ToastHost::Clear() noexcept {
    entries_.clear();
}

std::size_t ToastHost::Size() const noexcept {
    return entries_.size();
}

void InlineAlert(const std::string_view title, const std::string_view description, const FeedbackVariant variant) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec4 accent{AccentFor(variant, tokens)};
    const float width{ImGui::GetContentRegionAvail().x};
    const ImVec2 titleSize{ImGui::CalcTextSize(title.data(), title.data() + title.size())};
    const ImVec2 descriptionSize{
        ImGui::CalcTextSize(description.data(), description.data() + description.size(), false, width - metrics.spacingXl * 2.0F)};
    const float height{titleSize.y + descriptionSize.y + metrics.spacingXl};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ImGui::Dummy({width, height});
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    const ImVec4 background{accent.x, accent.y, accent.z, 0.10F};
    draw.AddRectFilled(minimum, {minimum.x + width, minimum.y + height}, ImGui::GetColorU32(background), metrics.controlRadius);
    draw.AddRect(minimum, {minimum.x + width, minimum.y + height}, ImGui::GetColorU32({accent.x, accent.y, accent.z, 0.35F}),
                 metrics.controlRadius);
    const std::string visibleTitle{title};
    const std::string visibleDescription{description};
    draw.AddText({minimum.x + metrics.spacingLg, minimum.y + metrics.spacingMd}, ImGui::GetColorU32(accent), visibleTitle.c_str());
    draw.AddText({minimum.x + metrics.spacingLg, minimum.y + metrics.spacingMd + titleSize.y + metrics.spacingXs},
                 ImGui::GetColorU32(tokens.mutedForeground), visibleDescription.c_str());
}

} // namespace px::ui
