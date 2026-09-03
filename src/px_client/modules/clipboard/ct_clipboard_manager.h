//
// Created by RGAA on 16/08/2024.
//

#ifndef GAMMARAYPC_CLIPBOARD_H
#define GAMMARAYPC_CLIPBOARD_H

#include <memory>
#include <QObject>
#include <objidl.h>
#include <wrl/client.h>
#include "px_common_new/clipboard/clipboard_echo.h"
#include "px_common_new/clipboard/clipboard_platform.h"
#include "px_message.pb.h"

namespace px
{

    class WinMessageLoop;
    class MessageListener;
    class CpVirtualFile;
    class ClipboardRuntimeBridge;

    class ClipboardManager : public QObject,
                             public std::enable_shared_from_this<ClipboardManager> {
    public:
        explicit ClipboardManager(
            std::shared_ptr<ClipboardRuntimeBridge> runtime_bridge);
        [[nodiscard]] bool Start();
        void Stop();
        void OnRemoteClipboardMessage(std::shared_ptr<px::Message> msg);
        void OnRemoteClipboardRespMessage(std::shared_ptr<px::Message> msg);
        void OnRemoteFileRespMessage(std::shared_ptr<px::Message> msg);
        void OnLocalClipboardUpdated();

    private:
        std::shared_ptr<ClipboardRuntimeBridge> runtime_bridge_;
        clipboard::EchoFilter echo_filter_;
        std::unique_ptr<clipboard::IPlatform> clipboard_platform_;
        std::shared_ptr<WinMessageLoop> msg_loop_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        Microsoft::WRL::ComPtr<CpVirtualFile> virtual_file_;
    };

}

#endif //GAMMARAYPC_CLIPBOARD_H
