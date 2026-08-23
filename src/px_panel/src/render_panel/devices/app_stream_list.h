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
#include <QProcess>
#include <mutex>
#include <atomic>
#include <functional>

#include "px_console_client/console_stream.h"

namespace px
{

    class PxContext;
    class PxSettings;
    class StreamDBOperator;
    class MessageListener;
    class RunningStreamManager;
    class StreamStateChecker;

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
        QListWidgetItem* AddItem(const std::shared_ptr<px_console::ConsoleStream>& item, int index);
        QWidget* GetItemByStreamId(const std::string& stream_id);
        void RegisterActions(int index, QListWidgetItem* cur_item);
        void ProcessAction(int index, QListWidgetItem* cur_item, const std::shared_ptr<px_console::ConsoleStream>& item);

        void CreateLayout();
        void Init();

        void DeleteStream(const std::shared_ptr<px_console::ConsoleStream>& item);
        void StartStream(QListWidgetItem* cur_item, const std::shared_ptr<px_console::ConsoleStream>& item, bool force_only_viewing);
        void StartFileTransfer(const std::shared_ptr<px_console::ConsoleStream>& item);
        void StartStreamInternal(QListWidgetItem* cur_item, const std::shared_ptr<px_console::ConsoleStream>& item, bool force_only_viewing);
        bool StopStream(const std::shared_ptr<px_console::ConsoleStream>& item);
        void LockDevice(const std::shared_ptr<px_console::ConsoleStream>& item);
        void RestartDevice(const std::shared_ptr<px_console::ConsoleStream>& item);
        void ShutdownDevice(const std::shared_ptr<px_console::ConsoleStream>& item);
        void EditStream(const std::shared_ptr<px_console::ConsoleStream>& item);
        void ShowSettings(const std::shared_ptr<px_console::ConsoleStream>& item);

        std::vector<std::shared_ptr<px_console::ConsoleStream>> CopyStreams();
        void RefreshRemoteDevices();
        void RefreshCloudApplications();
        void ClearIdentityResources();

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<StreamDBOperator> db_mgr_ = nullptr;
        std::mutex streams_mtx_;
        std::vector<std::shared_ptr<px_console::ConsoleStream>> streams_;
        std::vector<std::shared_ptr<px_console::ConsoleStream>> console_app_streams_;
        AppStreamListMode mode_ = AppStreamListMode::kRemoteDevices;
        std::function<void(bool)> on_empty_changed_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        QListWidget* stream_list_ = nullptr;
        std::shared_ptr<RunningStreamManager> running_stream_mgr_ = nullptr;
        // online state checker
        std::shared_ptr<StreamStateChecker> state_checker_ = nullptr;
        std::atomic_bool resource_refresh_inflight_ {false};

    };

}

#endif //SAILFISH_CLIENT_PC_APPSTREAMLIST_H
