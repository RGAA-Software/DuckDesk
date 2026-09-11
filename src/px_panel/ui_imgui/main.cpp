#include "panel_preview.h"

#include "px_desktop_shell/desktop_shell.h"

#include <utility>

int main() {
    auto shellResult = px::desktop::DesktopShell::Create({.title = "Pixels Panel UI Preview", .width = 1180, .height = 760});
    if (!shellResult) {
        return 1;
    }

    auto shell = std::move(shellResult.value());
    px::panel::ui::PanelPreview panel{};
    return shell.Run([&panel, &shell] {
        const auto action = panel.Draw();
        if (action.selectedTheme.has_value()) {
            shell.SetTheme(*action.selectedTheme);
        }
        if (action.exitRequested) {
            shell.RequestExit();
        }
    });
}
