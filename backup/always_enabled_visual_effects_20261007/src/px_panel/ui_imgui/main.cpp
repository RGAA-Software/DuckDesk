#include "panel_preview.h"

#include "px_desktop_shell/desktop_shell.h"

#include <utility>

int main() {
    auto shellResult = px::desktop::DesktopShell::Create({.title = "Pixels Panel UI Preview",
                                                          .width = 960,
                                                          .height = 640,
                                                          .showMinimizeButton = true,
                                                          .showMaximizeButton = false,
                                                          .allowTitleBarMaximize = false,
                                                          .useRoundedWindow = true,
                                                          .resizable = true});
    if (!shellResult) {
        return 1;
    }

    auto shell = std::move(shellResult.value());
    px::panel::ui::PanelPreview panel{};
    return shell.Run([&panel, &shell] {
        const auto action = panel.Draw(shell.PlatformIcons());
        if (action.selectedTheme.has_value()) {
            shell.SetTheme(*action.selectedTheme);
        }
        if (action.enhancedVisualEffects.has_value()) {
            shell.SetEnhancedVisualEffects(*action.enhancedVisualEffects);
        }
        if (action.exitRequested) {
            shell.RequestExit();
        }
    });
}
