//
// Created by RGAA on 2024/3/17.
//

#ifndef TC_APPLICATION_APP_SHARED_INFO_READER_H
#define TC_APPLICATION_APP_SHARED_INFO_READER_H

#include <memory>
#include <string>

#include "tc_capture_new/capture_message.h"

namespace tc
{

    // Reads hook bootstrap written by the render host (file under hook_boot/).
    // Not shared-memory — frame IPC is WebSocket /ipc.
    class AppSharedInfoReader {
    public:
        static std::shared_ptr<AppSharedInfoReader> Make(const std::string& name);

        explicit AppSharedInfoReader(const std::string& name);

        std::shared_ptr<AppSharedMessage> ReadData();
        void Exit();

    private:
        std::string name_;
    };

}

#endif //TC_APPLICATION_APP_SHARED_INFO_READER_H
