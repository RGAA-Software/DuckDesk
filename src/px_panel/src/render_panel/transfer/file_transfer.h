//
// Created by RGAA  on 8/07/2024.
//

#ifndef PX_FILETRANSFER_H
#define PX_FILETRANSFER_H
#ifndef ASIO2_ENABLE_SSL
#define ASIO2_ENABLE_SSL
#endif
#include <memory>
#include <asio2/asio2.hpp>

namespace px
{

    class PxContext;
    class PxSettings;
    class Thread;
    class File;

    // @Deprecated !!
    // Drag & Drop file transferring
    class FileTransferChannel {
    public:
        explicit FileTransferChannel(const std::shared_ptr<PxContext>& ctx, const std::shared_ptr<asio2::http_session>& sess);
        void OnConnected();
        void OnDisConnected();
        void ParseBinaryMessage(std::string_view data);
        void PostBinaryMessage(const std::string& msg);

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<File> transferring_file_ = nullptr;
        std::shared_ptr<asio2::http_session> sess_ = nullptr;
    };

}

#endif //PX_FILETRANSFER_H
