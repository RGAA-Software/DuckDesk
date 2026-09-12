#pragma once

#include "client_session.h"
#include "px_desktop_shell/desktop_shell.h"

#include <functional>
#include <chrono>
#include <memory>

namespace px::client::imgui {

class ClientFileTransferPanel;
class ClientToolbar;

class ClientWindow final {
  public:
    ClientWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session, bool english);
    ~ClientWindow();
    void Draw();
    void HandleInput(const px::desktop::DesktopInputEvent& event);

  private:
    [[nodiscard]] std::uint32_t VirtualKey(const px::desktop::DesktopInputEvent& event) const;
    [[nodiscard]] bool InVideo(float x, float y) const noexcept;
    void SynchronizeClipboard();

    std::reference_wrapper<px::desktop::DesktopShell> shell_;
    std::shared_ptr<ClientSession> session_{};
    std::shared_ptr<ClientFileTransferPanel> fileTransfer_{};
    std::unique_ptr<ClientToolbar> toolbar_{};
    std::shared_ptr<ClientVideoFrame> uploadedFrame_{};
    float videoLeft_{};
    float videoTop_{};
    float videoWidth_{};
    float videoHeight_{};
    bool english_{};
    bool darkTheme_{true};
    bool windowVisible_{};
    bool terminalErrorShown_{};
    bool terminalErrorPopupOpened_{};
    std::string clipboardText_{};
    std::chrono::steady_clock::time_point nextClipboardCheck_{};
};

} // namespace px::client::imgui
