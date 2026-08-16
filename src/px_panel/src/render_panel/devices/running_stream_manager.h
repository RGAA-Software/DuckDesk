//
// Created by RGAA on 28/03/2025.
//

#ifndef PX_RUNNING_STREAM_MANAGER_H
#define PX_RUNNING_STREAM_MANAGER_H

#include <memory>
#include <string>
#include <map>

#include <QProcess>
#include "px_cms_client/cms_stream.h"

namespace px
{

    class PxContext;
    class PxSettings;
    class StartStreamLoading;
    class MessageListener;
    class TcDialog;

    class RunningStreamManager : public std::enable_shared_from_this<RunningStreamManager> {
    public:
        explicit RunningStreamManager(const std::shared_ptr<PxContext>& ctx);
        ~RunningStreamManager();
        void InitMessageListeners();
        void StartStream(const std::shared_ptr<px_cms::CmsStream>& item, const std::string& network_type, bool direct);
        void StopStream(const std::shared_ptr<px_cms::CmsStream>& item);

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::map<std::string, std::shared_ptr<QProcess>> running_processes_;
        std::map<std::string, std::shared_ptr<StartStreamLoading>> loading_dialogs_;
        std::shared_ptr<TcDialog> no_conn_dialog_ = nullptr;
    };

}

#endif //PX_RUNNING_STREAM_MANAGER_H
