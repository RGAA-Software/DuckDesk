#include "client_startup_dialog.h"

#include "px_desktop_shell/desktop_shell.h"

#include <imgui.h>

#include <string>

namespace px::client::imgui {

StartupDialogAction ShowStartupDialog(const std::string_view message, const std::string_view button, const bool error) {
    auto shellResult = px::desktop::DesktopShell::Create(
        {.title = "Pixels Client", .width = 720, .height = 300, .initiallyVisible = true, .minimizeToTray = false});
    if (!shellResult) return StartupDialogAction::Exit;
    auto shell = std::move(shellResult.value());
    bool opened{};
    bool accepted{};
    const std::string messageText{message};
    const std::string buttonText{button};
    static_cast<void>(shell.Run([&shell, &opened, &accepted, &messageText, &buttonText, error] {
        if (!opened) {
            ImGui::OpenPopup("Pixels##startup-dialog");
            opened = true;
        }
        ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_Always, {0.5F, 0.5F});
        ImGui::SetNextWindowSize({620.0F, 0.0F}, ImGuiCond_Always);
        constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings};
        if (!ImGui::BeginPopupModal("Pixels##startup-dialog", nullptr, flags)) return;
        if (error) ImGui::PushStyleColor(ImGuiCol_Text, {0.95F, 0.30F, 0.30F, 1.0F});
        ImGui::TextWrapped("%s", messageText.c_str());
        if (error) ImGui::PopStyleColor();
        ImGui::Spacing();
        constexpr float buttonWidth{160.0F};
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5F);
        if (ImGui::Button(buttonText.c_str(), {buttonWidth, 0.0F})) {
            accepted = true;
            shell.RequestExit();
        }
        ImGui::EndPopup();
    }));
    return accepted ? StartupDialogAction::Continue : StartupDialogAction::Exit;
}

} // namespace px::client::imgui
