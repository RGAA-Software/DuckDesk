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
            px::ui::SectionTitle(error->title);
            ImGui::Spacing();
            ImGui::TextWrapped("%s", error->message.c_str());
            ImGui::Spacing();
            const float buttonWidth{px::ui::Scale(100.0F)};
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5F + ImGui::GetCursorPosX());
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
    if (ImGui::Begin("Notifications", {}, flags)) {
        for (auto item = active_.begin(); item != active_.end();) {
            if (item->level == NotificationLevel::Error) {
                ++item;
                continue;
            }
            ImGui::TextColored(tokens.primary, "%s", item->title.c_str());
            ImGui::TextWrapped("%s", item->message.c_str());
            const std::string dismissId{"OK##notification-" + std::to_string(item->id)};
            if (px::ui::ActionButton({dismissId}, "OK", {.variant = px::ui::ButtonVariant::Ghost, .size = px::ui::WidgetSize::Sm})) {
                if (item->action) {
                    item->action();
                }
                item = active_.erase(item);
            } else {
                ++item;
            }
            if (item != active_.end()) {
                ImGui::Separator();
            }
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
}

} // namespace px::panel::ui
