#pragma once

#include "client_local_file_system.h"
#include "client_session.h"
#include "px_desktop_shell/desktop_shell.h"
#include "px_ui/components/feedback.h"

#include <functional>
#include <memory>
#include <string>

namespace px::client::imgui {

class ClientFileTransferWindow final {
  public:
    ClientFileTransferWindow(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session, std::string remoteName,
                             bool english);
    void Draw();

  private:
    enum class FileOperation : std::uint8_t { None, CreateLocal, CreateRemote, RenameLocal, RenameRemote, DeleteLocal, DeleteRemote };

    void DrawLocalPane();
    void DrawRemotePane();
    void DrawTransferQueue();
    void DrawConnectionFailure(const ClientSessionSnapshot& snapshot);
    void DrawFileOperationDialog();
    void BeginOperation(FileOperation operation, std::string value = {});

    std::reference_wrapper<px::desktop::DesktopShell> shell_;
    std::shared_ptr<ClientSession> session_{};
    ClientLocalFileSystem localFiles_{};
    std::string remoteName_{};
    std::string localPath_{};
    std::string remotePath_{};
    std::string localSearch_{};
    std::string remoteSearch_{};
    std::string selectedLocal_{};
    std::string selectedRemote_{};
    std::string operationValue_{};
    std::string operationError_{};
    FileOperation operation_{FileOperation::None};
    bool selectedLocalDirectory_{};
    bool selectedRemoteDirectory_{};
    bool openOperationDialog_{};
    bool english_{};
    bool shown_{};
    bool errorPopupOpened_{};
    bool applyOverwriteToAll_{};
    px::ui::ToastHost toasts_{};
};

} // namespace px::client::imgui
