//
// Created by RGAA on 28/03/2025.
//

#ifndef GAMMARAY_RUNNING_STREAM_MANAGER_H
#define GAMMARAY_RUNNING_STREAM_MANAGER_H

#include <memory>
#include <string>
#include <map>

#include <QProcess>
#include "px_cms_client/cms_stream.h"

namespace px
{

    class GrContext;
    class GrSettings;
    class StartStreamLoading;
    class MessageListener;
    class TcDialog;

    class RunningStreamManager : public std::enable_shared_from_this<RunningStreamManager> {
    public:
        explicit RunningStreamManager(const std::shared_ptr<GrContext>& ctx);
        ~RunningStreamManager();
        void InitMessageListeners();
        void StartStream(const std::shared_ptr<px_cms::CmsStream>& item, const std::string& network_type, bool direct);
        void StopStream(const std::shared_ptr<px_cms::CmsStream>& item);

    private:
        GrSettings* settings_ = nullptr;
        std::shared_ptr<GrContext> context_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::map<std::string, std::shared_ptr<QProcess>> running_processes_;
        std::map<std::string, std::shared_ptr<StartStreamLoading>> loading_dialogs_;
        std::shared_ptr<TcDialog> no_conn_dialog_ = nullptr;
    };

}

#endif //GAMMARAY_RUNNING_STREAM_MANAGER_H
