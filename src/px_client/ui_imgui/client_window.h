#pragma once

#include "client_input_mapper.h"
#include "client_session.h"
#include "px_desktop_shell/desktop_shell.h"

#include <functional>
#include <chrono>
#include <array>
#include <memory>
#include <unordered_map>

namespace px::client::imgui {

class ClientFileTransferPanel;
class ClientToolbar;

class ClientWindow final {
  public:
    ClientWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session, bool english, bool darkTheme,
                 bool enhancedVisualEffects);
    ~ClientWindow();
    void Draw();
    void HandleInput(const px::desktop::DesktopInputEvent& event);

  private:
    [[nodiscard]] bool InVideo(float x, float y) const noexcept;
    void ReleasePressedInput();
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
    float lastMouseXRatio_{0.5F};
    float lastMouseYRatio_{0.5F};
    std::unordered_map<std::uint32_t, WindowsKey> pressedKeys_{};
    std::array<bool, 4> pressedMouseButtons_{};
    std::array<bool, 4> localPointerButtons_{};
    bool english_{};
    bool darkTheme_{true};
    bool windowVisible_{};
    bool terminalErrorShown_{};
    bool terminalErrorPopupOpened_{};
    bool textCompositionActive_{};
    std::string clipboardText_{};
    std::chrono::steady_clock::time_point nextClipboardCheck_{};
    std::chrono::steady_clock::time_point nextMouseRouteLog_{};
};

} // namespace px::client::imgui
