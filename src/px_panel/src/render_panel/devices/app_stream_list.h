//
// Created by RGAA on 2023/8/14.
//

#ifndef SAILFISH_CLIENT_PC_APPSTREAMLIST_H
#define SAILFISH_CLIENT_PC_APPSTREAMLIST_H

#include <QtWidgets/QWidget>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QLabel>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QListWidgetItem>
#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QString>
#include <QPaintEvent>
#include <map>
#include <unordered_map>
#include <QPointer>
#include <mutex>
#include <functional>
#include <optional>

#include "px_console_client/console_stream.h"
#include "stream_launch_auth_workflow.h"

namespace px
{

    class PxContext;
    class PxSettings;
    class StreamDBOperator;
    class MessageListener;
    class RunningStreamManager;
    class StreamStateChecker;
    class StreamItemWidget;
    class StreamResourceRefreshGate;

    enum class AppStreamListMode {
        kRemoteDevices,
        kCloudApplications,
    };

    using OnItemDoubleClickedCallback = std::function<void(const std::shared_ptr<px_console::ConsoleStream>&)>;

    class AppStreamList : public QWidget {
    public:
        explicit AppStreamList(const std::shared_ptr<PxContext>& ctx,
                               AppStreamListMode mode,
                               std::function<void(bool)> on_empty_changed,
                               QWidget* parent = nullptr);
        ~AppStreamList() override;

        void LoadStreamItems();
        void RefreshResources();

    private:
        void AddItem(const std::shared_ptr<px_console::ConsoleStream>& item, int index);
        QPointer<StreamItemWidget> GetItemByStreamId(const std::string& stream_id);
        void RegisterActions(int index);
        void ProcessAction(int index, const std::shared_ptr<px_console::ConsoleStream>& item);

        void CreateLayout();
        void Init();
        void StartResourceRefresh(bool identity_changed);

        void DeleteStream(const std::shared_ptr<px_console::ConsoleStream>& item);
        void StartStream(const std::shared_ptr<px_console::ConsoleStream>& item, bool force_only_viewing);
        void StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item);
        void StartFileTransferTicketLaunch(
            const std::shared_ptr<px_console::ConsoleStream>& target_item);
        void CompleteFileTransferTicketLaunch(
            const std::shared_ptr<px_console::ConsoleStream>& target_item,
            std::uint64_t generation,
            StreamLaunchAuthResult result);
        void StartStreamInternal(const std::shared_ptr<px_console::ConsoleStream>& item, bool force_only_viewing);
        void StartConsoleTicketLaunch(
            const std::shared_ptr<px_console::ConsoleStream>& target_item,
            bool uses_console_app_ticket);
        void CompleteConsoleTicketLaunch(
            const std::shared_ptr<px_console::ConsoleStream>& target_item,
            bool uses_console_app_ticket,
            std::uint64_t generation,
            StreamLaunchAuthResult result);
        void ContinueStartStream(
            const std::shared_ptr<px_console::ConsoleStream>& target_item,
            bool uses_console_ticket,
            std::optional<bool> authenticated_direct_available = std::nullopt);
        StreamLaunchAuthHooks MakeStreamLaunchAuthHooks() const;
        bool StopStream(const std::shared_ptr<px_console::ConsoleStream>& item);
        void LockDevice(const std::shared_ptr<px_console::ConsoleStream>& item);
        void RestartDevice(const std::shared_ptr<px_console::ConsoleStream>& item);
        void ShutdownDevice(const std::shared_ptr<px_console::ConsoleStream>& item);
        void EditStream(const std::shared_ptr<px_console::ConsoleStream>& item);
        void ShowSettings(const std::shared_ptr<px_console::ConsoleStream>& item);

        std::vector<std::shared_ptr<px_console::ConsoleStream>> CopyStreams();
        void ClearIdentityResources();

    private:
        std::reference_wrapper<PxSettings> settings_;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<StreamDBOperator> db_mgr_ = nullptr;
        std::mutex streams_mtx_;
        std::vector<std::shared_ptr<px_console::ConsoleStream>> streams_;
        std::vector<std::shared_ptr<px_console::ConsoleStream>> console_app_streams_;
        std::unordered_map<std::string, bool> console_device_online_states_;
        AppStreamListMode mode_ = AppStreamListMode::kRemoteDevices;
        std::function<void(bool)> on_empty_changed_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        QPointer<QListWidget> stream_list_;
        std::shared_ptr<RunningStreamManager> running_stream_mgr_ = nullptr;
        std::shared_ptr<StreamLaunchAuthWorkflow> stream_launch_auth_workflow_ = nullptr;
        // online state checker
        std::shared_ptr<StreamStateChecker> state_checker_ = nullptr;
        std::shared_ptr<StreamResourceRefreshGate> resource_refresh_gate_ = nullptr;

    };

}

#endif //SAILFISH_CLIENT_PC_APPSTREAMLIST_H
