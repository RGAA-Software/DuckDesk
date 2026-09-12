#include "client_launch_config.h"
#include "client_session.h"
#include "client_startup_dialog.h"
#include "client_window.h"

#include "px_desktop_shell/desktop_shell.h"

#include <Windows.h>
#include <iostream>
#include <iterator>
#include <memory>

int main() {
    std::string envelope{std::istreambuf_iterator<char>{std::cin}, std::istreambuf_iterator<char>{}};
    const auto config = px::client::imgui::ParseClientLaunchEnvelope(envelope);
    if (!envelope.empty())
        SecureZeroMemory(envelope.data(), envelope.size());
    if (!config) {
        static_cast<void>(px::client::imgui::ShowStartupDialog(
            "Pixels Client received an invalid or incomplete launch request.\nPixels Client 收到了无效或不完整的启动请求。", "OK / 确定", true));
        return 2;
    }
    if (config->waitForDebugger) {
        const bool english = config->language == "en-US";
        if (px::client::imgui::ShowStartupDialog(english ? "Attach the debugger, then continue." : "请附加调试器，然后继续。",
                                                 english ? "Continue" : "继续", false) == px::client::imgui::StartupDialogAction::Exit) {
            return 0;
        }
    }
    auto shellResult = px::desktop::DesktopShell::Create({.title = config->streamName.empty() ? "Pixels Client" : "Pixels - " + config->streamName,
                                                          .width = 1440,
                                                          .height = 900,
                                                          .initiallyVisible = false});
    const bool english = config->language == "en-US";
    if (!shellResult) {
        static_cast<void>(px::client::imgui::ShowStartupDialog(
            english ? "Pixels Client could not create its window or graphics device. Update the graphics driver, then retry."
                    : "Pixels Client 无法创建窗口或图形设备。请更新显卡驱动后重试。",
            english ? "OK" : "确定", true));
        return 3;
    }
    auto shell = std::move(shellResult.value());
    auto session = px::client::imgui::ClientSession::Create(*config);
    if (!session) {
        static_cast<void>(px::client::imgui::ShowStartupDialog(
            english ? "Pixels Client could not initialize this connection. Check the launch data and installed runtime files, then retry."
                    : "Pixels Client 无法初始化本次连接。请检查启动数据和已安装的运行库文件后重试。",
            english ? "OK" : "确定", true));
        return 4;
    }
    px::client::imgui::ClientWindow window{std::ref(shell), session, english};
    session->Start();
    const int result = shell.Run([&window] { window.Draw(); }, [&window](const px::desktop::DesktopInputEvent& event) { window.HandleInput(event); });
    session->Stop();
    return result;
}
