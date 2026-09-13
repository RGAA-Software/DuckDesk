#include "client_startup_dialog.h"

#include "px_desktop_shell/desktop_shell.h"
#include "px_ui/components/button.h"
#include "px_ui/components/overlay.h"
#include "px_ui/theme_tokens.h"

#include <imgui.h>

#include <string>

namespace px::client::imgui {

StartupDialogAction ShowStartupDialog(const std::string_view message, const std::string_view button, const bool error) {
    auto shellResult = px::desktop::DesktopShell::Create({.title = "Pixels Client",
                                                          .width = 720,
                                                          .height = 300,
                                                          .minimumWidth = 640,
                                                          .minimumHeight = 280,
                                                          .initiallyVisible = true,
                                                          .minimizeToTray = false,
                                                          .showMinimizeButton = false,
                                                          .showMaximizeButton = false,
                                                          .allowTitleBarMaximize = false,
                                                          .useRoundedWindow = true,
                                                          .resizable = false});
    if (!shellResult)
        return StartupDialogAction::Exit;
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
        constexpr ImGuiWindowFlags flags{ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings};
        const px::ui::ModalScope modal{{"Pixels##startup-dialog"}, 620.0F, flags};
        if (!modal.Open())
            return;
        if (error)
            ImGui::PushStyleColor(ImGuiCol_Text, px::ui::CurrentThemeTokens().destructive);
        ImGui::TextWrapped("%s", messageText.c_str());
        if (error)
            ImGui::PopStyleColor();
        ImGui::Spacing();
        constexpr float buttonWidth{160.0F};
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - buttonWidth) * 0.5F);
        if (px::ui::ActionButton({"startup-dialog-action"}, buttonText, {.width = buttonWidth})) {
            accepted = true;
            shell.RequestExit();
        }
    }));
    return accepted ? StartupDialogAction::Continue : StartupDialogAction::Exit;
}

} // namespace px::client::imgui
