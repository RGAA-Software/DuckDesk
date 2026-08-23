//
// Created by RGAA on 28/03/2025.
//

#ifndef PX_RUNNING_STREAM_MANAGER_H
#define PX_RUNNING_STREAM_MANAGER_H

#include <memory>
#include <string>
#include <map>
#include <mutex>

#include <QProcess>
#include "px_console_client/console_stream.h"

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
        void StartStream(const std::shared_ptr<px_console::ConsoleStream>& item, const std::string& network_type, bool direct);
        bool OpenFileTransferInRunningClient(const std::shared_ptr<px_console::ConsoleStream>& item);
        void StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item, const std::string& network_type);
        // False means the user cancelled closing a running local client.
        bool StopStream(const std::shared_ptr<px_console::ConsoleStream>& item);

    private:
        bool RefreshConsoleTicket(const std::shared_ptr<px_console::ConsoleStream>& item);
        void RestartActiveRtcSessions(uint64_t revision);
        void RestartRtcSession(const std::string& stream_id, uint64_t revision);
        void FallbackDirectRtc(const std::string& stream_id, const char* reason);

        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        std::map<std::string, std::shared_ptr<QProcess>> running_processes_;
        std::map<std::string, std::shared_ptr<StartStreamLoading>> loading_dialogs_;
        std::map<std::string, std::shared_ptr<px_console::ConsoleStream>> running_items_;
        std::map<std::string, std::string> running_network_types_;
        std::map<std::string, bool> running_connected_;
        std::mutex running_mutex_;
        std::shared_ptr<TcDialog> no_conn_dialog_ = nullptr;
    };

}

#endif //PX_RUNNING_STREAM_MANAGER_H
