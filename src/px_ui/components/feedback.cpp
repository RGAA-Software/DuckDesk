#include "px_ui/components/feedback.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "px_ui/components/button.h"
#include "px_ui/components/surface.h"
#include "px_ui/theme_tokens.h"

namespace px::ui {
namespace {

ImVec4 AccentFor(const FeedbackVariant variant, const ThemeTokens& tokens) noexcept {
    switch (variant) {
        case FeedbackVariant::Success:
            return tokens.success;
        case FeedbackVariant::Warning:
            return tokens.warning;
        case FeedbackVariant::Error:
            return tokens.destructiveText;
        case FeedbackVariant::Info:
        default:
            return tokens.primaryText;
    }
}

VectorIcon IconFor(const FeedbackVariant variant) noexcept {
    switch (variant) {
        case FeedbackVariant::Success:
            return VectorIcon::CircleCheck;
        case FeedbackVariant::Warning:
        case FeedbackVariant::Error:
            return VectorIcon::TriangleAlert;
        case FeedbackVariant::Info:
        default:
            return VectorIcon::Info;
    }
}

}  // namespace

void ToastHost::Push(ToastMessage message) {
    const auto now{std::chrono::steady_clock::now()};
    const auto expiresAt{now + message.duration};
    entries_.push_back(Entry{.id = nextId_++, .message = std::move(message), .expiresAt = expiresAt});
    constexpr std::size_t maximumEntries{5};
    while (entries_.size() > maximumEntries) {
        entries_.pop_front();
    }
}

bool ToastHost::CapturesPointer(const float pointX, const float pointY) const noexcept {
    const auto now{std::chrono::steady_clock::now()};
    return std::ranges::any_of(entries_, [now, pointX, pointY](const Entry& entry) {
        return now < entry.expiresAt && pointX >= entry.minimum.x && pointY >= entry.minimum.y && pointX < entry.maximum.x &&
               pointY < entry.maximum.y;
    });
}

void ToastHost::Draw(const ToastPlacement placement, const float topInset) {
    const auto now{std::chrono::steady_clock::now()};
    std::erase_if(entries_, [now](const Entry& entry) { return now >= entry.expiresAt; });
    if (entries_.empty()) {
        return;
    }
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImGuiViewport& viewport{*ImGui::GetMainViewport()};
    float bottom{viewport.WorkPos.y + viewport.WorkSize.y - metrics.spacingLg};
    float top{viewport.WorkPos.y + std::max(0.0F, topInset) + metrics.spacingLg};
    std::vector<std::uint64_t> dismissed{};
    std::vector<std::function<void()>> clickActions{};
    for (auto iterator{entries_.rbegin()}; iterator != entries_.rend(); ++iterator) {
        Entry& entry{*iterator};
        const float width{std::min(360.0F * metrics.scale, std::max(1.0F, viewport.WorkSize.x - metrics.spacingLg * 2.0F))};
        const float textWidth{
            std::max(1.0F, width - metrics.spacingLg * 2.0F - metrics.iconDefault - metrics.spacingSm - metrics.controlXs - metrics.spacingSm)};
        const ImVec2 titleSize{ImGui::CalcTextSize(entry.message.title.c_str(), nullptr, false, textWidth)};
        const ImVec2 descriptionSize{ImGui::CalcTextSize(entry.message.description.c_str(), nullptr, false, textWidth)};
        const float titleHeight{std::max(titleSize.y, metrics.controlXs)};
        const float height{titleHeight + metrics.spacingMd * 2.0F +
                           (entry.message.description.empty() ? 0.0F : descriptionSize.y + metrics.spacingXs)};
        bottom -= height;
        const ImVec2 position{viewport.WorkPos.x + viewport.WorkSize.x - width - metrics.spacingLg,
                              placement == ToastPlacement::TopRight ? top : bottom};
        entry.minimum = position;
        entry.maximum = {position.x + width, position.y + height};
        ImGui::SetNextWindowPos(position);
        ImGui::SetNextWindowSize({width, height});
        ImVec4 background{tokens.popover};
        background.w = EnhancedVisualEffectsEnabled() ? 0.96F : 1.0F;
        ImGui::PushStyleColor(ImGuiCol_WindowBg, background);
        ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, metrics.popupRadius);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{metrics.spacingLg, metrics.spacingMd});
        const std::string windowId{"##px-toast-" + std::to_string(entry.id)};
        if (ImGui::Begin(windowId.c_str(), {},
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoNavFocus |
                             ImGuiWindowFlags_NoFocusOnAppearing)) {
            const ImVec4 accent{AccentFor(entry.message.variant, tokens)};
            const ImVec2 start{ImGui::GetCursorScreenPos()};
            DrawVectorIcon(IconFor(entry.message.variant), start, metrics.iconDefault, ImGui::GetColorU32(accent));
            ImGui::SetCursorScreenPos({start.x + metrics.iconDefault + metrics.spacingSm, start.y});
            ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
            StrongText(entry.message.title);
            ImGui::PopTextWrapPos();
            ImGui::SetCursorScreenPos({start.x + width - metrics.spacingLg * 2.0F - metrics.controlXs, start.y});
            const std::string closeId{"toast-close-" + std::to_string(entry.id)};
            if (IconAction({closeId}, VectorIcon::Close, {},
                           {.variant = ButtonVariant::GhostDestructive, .size = WidgetSize::IconXs, .circular = true})) {
                dismissed.push_back(entry.id);
            }
            const bool overCloseButton{ImGui::IsItemHovered()};
            if (!entry.message.description.empty()) {
                ImGui::SetCursorScreenPos({start.x + metrics.iconDefault + metrics.spacingSm, start.y + titleHeight + metrics.spacingXs});
                ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + textWidth);
                ImGui::PushStyleColor(ImGuiCol_Text, tokens.mutedForeground);
                ImGui::TextWrapped("%s", entry.message.description.c_str());
                ImGui::PopStyleColor();
                ImGui::PopTextWrapPos();
            }
            if (ImGui::IsWindowHovered() && entry.message.onClick && !overCloseButton) {
                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                // Keep the actionable path readable while the user is pointing at it.
                entry.expiresAt = std::chrono::steady_clock::now() + entry.message.duration;
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    clickActions.push_back(entry.message.onClick);
                    dismissed.push_back(entry.id);
                }
            }
        }
        ImGui::End();
        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(2);
        bottom -= metrics.spacingSm;
        top += height + metrics.spacingSm;
    }
    std::erase_if(entries_, [&dismissed](const Entry& entry) { return std::ranges::find(dismissed, entry.id) != dismissed.end(); });
    // Run value-owned actions only after iteration, so an action can safely push/clear host messages.
    for (const auto& clickAction : clickActions) clickAction();
}

void ToastHost::Clear() noexcept { entries_.clear(); }

std::size_t ToastHost::Size() const noexcept { return entries_.size(); }

void InlineAlert(const std::string_view title, const std::string_view description, const FeedbackVariant variant) {
    const ThemeTokens tokens{CurrentThemeTokens()};
    const UiMetrics metrics{MetricsFor(ImGui::GetStyle().FontScaleDpi)};
    const ImVec4 accent{AccentFor(variant, tokens)};
    const float width{ImGui::GetContentRegionAvail().x};
    const float textWidth{std::max(1.0F, width - metrics.spacingMd * 2.0F - metrics.iconDefault - metrics.spacingSm)};
    const ImVec2 titleSize{ImGui::CalcTextSize(title.data(), title.data() + title.size(), false, textWidth)};
    const ImVec2 descriptionSize{ImGui::CalcTextSize(description.data(), description.data() + description.size(), false, textWidth)};
    const float descriptionGap{description.empty() ? 0.0F : metrics.spacingXs};
    const float height{std::max(titleSize.y, metrics.iconDefault) + descriptionSize.y + descriptionGap + metrics.spacingMd * 2.0F};
    const ImVec2 minimum{ImGui::GetCursorScreenPos()};
    ImGui::Dummy({width, height});
    ImDrawList& draw{*ImGui::GetWindowDrawList()};
    const ImVec4 background{accent.x, accent.y, accent.z, 0.10F};
    draw.AddRectFilled(minimum, {minimum.x + width, minimum.y + height}, ImGui::GetColorU32(background), metrics.controlRadius);
    draw.AddRect(minimum, {minimum.x + width, minimum.y + height}, ImGui::GetColorU32({accent.x, accent.y, accent.z, 0.35F}), metrics.controlRadius);
    const float iconSize{metrics.iconDefault};
    const float textLeft{minimum.x + metrics.spacingMd + iconSize + metrics.spacingSm};
    DrawVectorIcon(IconFor(variant), {minimum.x + metrics.spacingMd, minimum.y + metrics.spacingMd}, iconSize, ImGui::GetColorU32(accent));
    const std::string visibleTitle{title};
    const std::string visibleDescription{description};
    // Dear ImGui borrows the current font for this synchronous drawing boundary.
    draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), {textLeft, minimum.y + metrics.spacingMd}, ImGui::GetColorU32(accent), visibleTitle.c_str(),
                 nullptr, textWidth);
    draw.AddText(ImGui::GetFont(), ImGui::GetFontSize(), {textLeft, minimum.y + metrics.spacingMd + titleSize.y + descriptionGap},
                 ImGui::GetColorU32(tokens.mutedForeground), visibleDescription.c_str(), nullptr, textWidth);
}

}  // namespace px::ui
