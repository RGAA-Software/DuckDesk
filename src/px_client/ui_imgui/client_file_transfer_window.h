#pragma once

#include "client_file_browser_model.h"
#include "client_local_file_system.h"
#include "client_session.h"
#include "px_desktop_shell/desktop_shell.h"
#include "px_ui/components/feedback.h"
#include "px_ui/device_platform.h"

#include <functional>
#include <memory>
#include <string>

namespace px::client::imgui {

class ClientFileTransferWindow final {
  public:
    ClientFileTransferWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session, std::string remoteName,
                             px::ui::DevicePlatform remotePlatform, bool english);
    void Draw();
    void HandleInput(const px::desktop::DesktopInputEvent& event);

  private:
    enum class FileOperation : std::uint8_t { None, CreateLocal, CreateRemote, RenameLocal, RenameRemote, DeleteLocal, DeleteRemote };

    void DrawLocalPane();
    void DrawRemotePane();
    void DrawTransferQueue();
    void DrawConnectionFailure(const ClientSessionSnapshot& snapshot);
    void DrawFileOperationDialog();
    void DrawCloseConfirmation();
    void DrawLocalLocationPicker(float width);
    void DrawRemoteLocationPicker(float width);
    void BeginOperation(FileOperation operation, std::string value = {});
    void NavigateRemote(std::string path, bool addHistory);
    [[nodiscard]] std::vector<ClientFileListItem> VisibleLocalItems() const;
    [[nodiscard]] std::vector<ClientFileListItem> VisibleRemoteItems() const;

    std::reference_wrapper<px::desktop::DesktopShell> shell_;
    std::shared_ptr<ClientSession> session_{};
    ClientLocalFileSystem localFiles_{};
    std::string remoteName_{};
    px::ui::DevicePlatform remotePlatform_{px::ui::DevicePlatform::Unknown};
    std::string localPath_{};
    std::string remotePath_{};
    ClientFileSelection localSelection_{};
    ClientFileSelection remoteSelection_{};
    ClientFileSort localSort_{};
    ClientFileSort remoteSort_{};
    std::vector<std::string> remoteHistory_{};
    std::string operationValue_{};
    std::string operationError_{};
    FileOperation operation_{FileOperation::None};
    bool openOperationDialog_{};
    bool english_{};
    bool shown_{};
    bool errorPopupOpened_{};
    bool applyOverwriteToAll_{};
    bool showHiddenLocal_{};
    bool showHiddenRemote_{};
    bool localPathEditing_{};
    bool remotePathEditing_{};
    bool localPathFocusRequested_{};
    bool remotePathFocusRequested_{};
    bool openCloseConfirmation_{};
    px::ui::ToastHost toasts_{};
};

} // namespace px::client::imgui
