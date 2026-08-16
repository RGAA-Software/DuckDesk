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

#include "px_cms_client/cms_stream.h"

namespace px
{

    class PxContext;
    class PxSettings;
    class StreamDBOperator;
    class StreamContent;
    class MessageListener;
    class RunningStreamManager;
    class StreamStateChecker;

    using OnItemDoubleClickedCallback = std::function<void(const std::shared_ptr<px_cms::CmsStream>&)>;

    class AppStreamList : public QWidget {
    public:
        explicit AppStreamList(const std::shared_ptr<PxContext>& ctx, QWidget* parent = nullptr);
        ~AppStreamList() override;

        void LoadStreamItems();
        void RequestBindDevices();

    private:
        QListWidgetItem* AddItem(const std::shared_ptr<px_cms::CmsStream>& item, int index);
        QWidget* GetItemByStreamId(const std::string& stream_id);
        void RegisterActions(int index, QListWidgetItem* cur_item);
        void ProcessAction(int index, QListWidgetItem* cur_item, const std::shared_ptr<px_cms::CmsStream>& item);

        void CreateLayout();
        void Init();

        void DeleteStream(const std::shared_ptr<px_cms::CmsStream>& item);
        void StartStream(QListWidgetItem* cur_item, const std::shared_ptr<px_cms::CmsStream>& item, bool force_only_viewing);
        void StartStreamInternal(QListWidgetItem* cur_item, const std::shared_ptr<px_cms::CmsStream>& item, bool force_only_viewing);
        void StopStream(const std::shared_ptr<px_cms::CmsStream>& item);
        void LockDevice(const std::shared_ptr<px_cms::CmsStream>& item);
        void RestartDevice(const std::shared_ptr<px_cms::CmsStream>& item);
        void ShutdownDevice(const std::shared_ptr<px_cms::CmsStream>& item);
        void EditStream(const std::shared_ptr<px_cms::CmsStream>& item);
        void ShowSettings(const std::shared_ptr<px_cms::CmsStream>& item);

        std::vector<std::shared_ptr<px_cms::CmsStream>> CopyStreams();

    private:
        PxSettings* settings_ = nullptr;
        std::shared_ptr<PxContext> context_ = nullptr;
        std::shared_ptr<StreamDBOperator> db_mgr_ = nullptr;
        std::mutex streams_mtx_;
        std::vector<std::shared_ptr<px_cms::CmsStream>> streams_;
        std::shared_ptr<MessageListener> msg_listener_ = nullptr;
        QListWidget* stream_list_ = nullptr;
        StreamContent* stream_content_ = nullptr;
        std::shared_ptr<RunningStreamManager> running_stream_mgr_ = nullptr;
        // online state checker
        std::shared_ptr<StreamStateChecker> state_checker_ = nullptr;

    };

}

#endif //SAILFISH_CLIENT_PC_APPSTREAMLIST_H
