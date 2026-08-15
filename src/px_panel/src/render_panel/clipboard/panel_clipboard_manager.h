//
// Created by RGAA on 16/08/2024.
//

#ifndef GAMMARAYPC_CLIPBOARD_H
#define GAMMARAYPC_CLIPBOARD_H

#include <memory>
#include <QObject>
#include <objidl.h>
#include "px_common_new/clipboard/clipboard_platform.h"

namespace tc
{
    class Message;
    class GrContext;
    class CpVirtualFile;

    class MsgClipboardEvent;

    class ClipboardManager : public QObject {
    public:
        explicit ClipboardManager(const std::shared_ptr<GrContext>& ctx);
        // Client -> Network -> Render -> Render Panel
        void OnRemoteClipboardInfo(std::shared_ptr<Message> msg);

    private:
        std::shared_ptr<GrContext> context_ = nullptr;
        std::unique_ptr<clipboard::IPlatform> clipboard_platform_;
        CpVirtualFile* virtual_file_ = nullptr;
        IDataObject* data_object_ = nullptr;
    };

}

#endif //GAMMARAYPC_CLIPBOARD_H
