#include "component_gallery.h"

#include "px_desktop_shell/desktop_shell.h"

#include <utility>

int main() {
    auto shellResult = px::desktop::DesktopShell::Create({.title = "Pixels UI Component Gallery",
                                                          .width = 1180,
                                                          .height = 760,
                                                          .showMinimizeButton = true,
                                                          .showMaximizeButton = false,
                                                          .allowTitleBarMaximize = false,
                                                          .useRoundedWindow = true,
                                                          .resizable = true});
    if (!shellResult) {
        return 1;
    }
    auto shell{std::move(shellResult.value())};
    px::panel::ui::ComponentGallery gallery{};
    return shell.Run([&gallery, &shell] {
        const auto action{gallery.Draw()};
        if (action.themeChanged) {
            shell.SetTheme(action.theme);
        }
        if (action.enhancedVisualEffectsChanged) {
            shell.SetEnhancedVisualEffects(action.enhancedVisualEffects);
        }
        if (action.exitRequested) {
            shell.RequestExit();
        }
    });
}
