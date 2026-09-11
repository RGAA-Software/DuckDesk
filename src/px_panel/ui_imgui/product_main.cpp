#include "panel_network_settings_port.h"
#include "panel_server_status_port.h"
#include "panel_preview.h"

#include "px_desktop_shell/desktop_shell.h"

#include "px_common/folder_util.h"
#include "px_common/log.h"
#include "px_common/shared_preference.h"
#include "render_panel/px_application.h"
#include "render_panel/px_settings.h"

#include <QApplication>
#include <QCoreApplication>

#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>
#include <utility>

namespace {

bool PrepareRuntimeDirectories(const std::filesystem::path& basePath) {
    constexpr std::array directories{"px_logs", "px_data", "px_data/client", "px_data/render", "px_data/panel", "px_data/cache", "px_dumps"};
    std::error_code error{};
    for (const auto directory : directories) {
        std::filesystem::create_directories(basePath / directory, error);
        if (error) {
            return false;
        }
    }
    return true;
}

} // namespace

int main(int argc, char* argv[]) { // NOLINT(gammaray-raw-pointer-boundary): process-entry ABI and transient Qt argument boundary
    QApplication qtApplication{argc, argv};
    const std::filesystem::path basePath{px::FolderUtil::GetProgramDataPath()};
    if (!PrepareRuntimeDirectories(basePath)) {
        return 1;
    }
    px::Logger::InitLog((basePath / "px_logs" / "pixels.log").wstring(), true);
    if (!px::SharedPreference::Instance()->Init(basePath / "px_data", "pixels.dat")) {
        return 2;
    }
    px::PxSettings::Instance()->px_data_path_ = (basePath / "px_data").string();

    const auto application = px::PxApplication::Make(nullptr, false);
    auto shellResult = px::desktop::DesktopShell::Create({.title = "Pixels", .width = 1180, .height = 760});
    if (!shellResult) {
        application->Exit();
        return 3;
    }

    int result{};
    {
        auto shell = std::move(shellResult.value());
        px::panel::ui::PanelPreview panel{{
            .networkSettings = px::panel::ui::CreatePanelNetworkSettingsPort(application),
            .serverStatus = px::panel::ui::CreatePanelServerStatusPort(application),
        }};
        result = shell.Run([&qtApplication, &panel, &shell] {
            qtApplication.processEvents(QEventLoop::AllEvents, 2);
            const auto action = panel.Draw();
            if (action.selectedTheme.has_value()) {
                shell.SetTheme(*action.selectedTheme);
            }
            if (action.exitRequested) {
                shell.RequestExit();
            }
        });
    }
    LOGI("ImGui Panel shell exited, beginning business shutdown");
    application->Exit();
    LOGI("ImGui Panel business shutdown completed");
    px::grApp.reset();
    return result;
}
