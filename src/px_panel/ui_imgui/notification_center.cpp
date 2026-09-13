#include "notification_center.h"

#include "px_ui/components/button.h"
#include "px_ui/components/overlay.h"
#include "px_ui/components/surface.h"
#include "px_ui/layout_metrics.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <algorithm>
#include <utility>

namespace px::panel::ui {

void NotificationCenter::Publish(PanelNotification notification) {
    const std::scoped_lock lock{mutex_};
    notification.id = nextId_++;
    pending_.push_back(std::move(notification));
}

void NotificationCenter::Draw() {
    {
        const std::scoped_lock lock{mutex_};
        while (!pending_.empty()) {
            active_.push_back(std::move(pending_.front()));
            pending_.pop_front();
        }
    }
    const auto now = std::chrono::steady_clock::now();
    std::erase_if(active_, [now](const PanelNotification& item) {
        return item.level == NotificationLevel::Information && now - item.createdAt > std::chrono::seconds{8};
    });
    if (active_.empty()) {
        return;
    }
    const ImGuiViewport& viewport{*ImGui::GetMainViewport()};
    if (const auto error = std::ranges::find(active_, NotificationLevel::Error, &PanelNotification::level); error != active_.end()) {
        const std::string popupId{error->title + "##notification-error-" + std::to_string(error->id)};
        px::ui::OpenModal({popupId});
        px::ui::ModalScope modal{{popupId}, 460.0F, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings};
        if (modal.Open()) {
            static_cast<void>(
                px::ui::DialogHeader({"notification-error-close"}, error->title, error->message,
                                     {.icon = px::ui::VectorIcon::TriangleAlert, .tone = px::ui::BadgeVariant::Destructive, .closeable = false}));
            const float buttonWidth{px::ui::Scale(100.0F)};
            px::ui::DialogFooter(buttonWidth);
            if (px::ui::ActionButton({"notification-error-ok"}, "OK", {.width = buttonWidth})) {
                if (error->action) {
                    error->action();
                }
                active_.erase(error);
                ImGui::CloseCurrentPopup();
            }
        }
    }

    const bool hasInformation{
        std::ranges::any_of(active_, [](const PanelNotification& item) { return item.level == NotificationLevel::Information; })};
    if (!hasInformation) {
        return;
    }
    ImVec2 position{viewport.WorkPos.x + viewport.WorkSize.x - px::ui::Scale(18.0F), viewport.WorkPos.y + viewport.WorkSize.y - px::ui::Scale(18.0F)};
    ImGui::SetNextWindowPos(position, ImGuiCond_Always, ImVec2{1.0F, 1.0F});
    ImGui::SetNextWindowSizeConstraints(ImVec2{px::ui::Scale(280.0F), 0.0F}, ImVec2{px::ui::Scale(460.0F), px::ui::Scale(320.0F)});
    constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings};
    const px::ui::ThemeTokens tokens{px::ui::CurrentThemeTokens()};
    ImGui::PushStyleColor(ImGuiCol_WindowBg, tokens.popover);
    ImGui::PushStyleColor(ImGuiCol_Border, tokens.border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, px::ui::Scale(10.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2{px::ui::Scale(14.0F), px::ui::Scale(12.0F)});
    if (ImGui::Begin("Notifications", {}, flags)) {
        for (auto item = active_.begin(); item != active_.end();) {
            if (item->level == NotificationLevel::Error) {
                ++item;
                continue;
            }
            const ImVec2 start{ImGui::GetCursorScreenPos()};
            px::ui::DrawVectorIcon(px::ui::VectorIcon::CircleCheck, start, px::ui::Scale(18.0F), ImGui::GetColorU32(tokens.success));
            ImGui::SetCursorScreenPos({start.x + px::ui::Scale(28.0F), start.y});
            px::ui::StrongText(item->title);
            const std::string dismissId{"dismiss##notification-" + std::to_string(item->id)};
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - px::ui::Scale(24.0F));
            const bool dismissed{px::ui::IconAction({dismissId}, px::ui::VectorIcon::Close, {},
                                                    {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::IconXs, .circular = true})};
            ImGui::SetCursorScreenPos({start.x + px::ui::Scale(28.0F), ImGui::GetItemRectMax().y + px::ui::Scale(2.0F)});
            ImGui::PushTextWrapPos(ImGui::GetWindowContentRegionMax().x - px::ui::Scale(8.0F));
            px::ui::MutedText(item->message);
            ImGui::PopTextWrapPos();
            if (dismissed) {
                if (item->action) {
                    item->action();
                }
                item = active_.erase(item);
            } else {
                ++item;
            }
            if (item != active_.end()) {
                px::ui::MenuSeparator();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}

} // namespace px::panel::ui
