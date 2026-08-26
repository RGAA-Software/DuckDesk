//
// Created by RGAA on 2023-12-27.
//

#include "px_client/ct_client_context.h"

#include "px_client/ct_settings.h"
#include "px_common_new/message_notifier.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/thread.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/hardware.h"
#include "px_common_new/folder_util.h"
#include "px_qt_widget/notify/notifymanager.h"
#include "px_client/ct_app_message.h"
#include <QTimer>
#include <QApplication>

namespace px
{

    static std::string kClientEmbedName = "ui.embed";

    ClientContext::ClientContext(const std::string& name, QObject* parent) : QObject(parent) {
        this->name_ = name;
        this->msg_notifier_ = std::make_shared<MessageNotifier>();
    }

    ClientContext::~ClientContext() {
        Exit();
    }

    void ClientContext::Init() {
        auto data_path = FolderUtil::GetProgramDataPath();
        auto log_path = std::format(L"{}/px_logs/app.{}.log", data_path, StringUtil::ToWString(this->name_));

        if (this->name_ == kClientEmbedName) {
            // embed in main panel
            // log to gammaray.log
            // will set device id by SetDeviceId
        }
        else {
            // single running
            Logger::InitLog(log_path, true);
        }
        LOGI("ClientContext in {}", this->name_);

        sp_ = SharedPreference::Instance();
        auto sp_name = std::format("app.{}.dat", this->name_);
        if (!sp_->Init(data_path + L"/px_data", sp_name)) {
            LOGE("!! Init sp failed: {}", sp_name);
        }
        else {
            LOGI("** Init app data success: {}", sp_name);
        }

        auto settings = Settings::Instance();
        //if (!render) {
        //    settings->LoadMainSettings();
        //} else {
            settings->LoadSettings();
        //}

        task_thread_ = Thread::Make("context_thread", 128);
        task_thread_->Poll();

        LOGI("Client params for: {}", this->name_);
        settings->Dump();

        const auto weak_self = weak_from_this();
        PostTask([weak_self]() {
            if (weak_self.expired()) return;
            auto hardware = Hardware::Instance();
            hardware->Detect(false, true, false);
            hardware->Dump();
        });
    }

    void ClientContext::PostTask(std::function<void()>&& task) {
        if (!exiting_.load(std::memory_order_acquire) && task_thread_) {
            task_thread_->Post(SimpleThreadTask::Make(std::move(task)));
        }
    }

    void ClientContext::PostUITask(std::function<void()>&& task) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, task = std::move(task)]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            task();
        });
    }

    void ClientContext::PostDelayUITask(std::function<void()>&& task, int ms) {
        auto weak_self = weak_from_this();
        this->PostUITask([weak_self, ms, t = std::move(task)]() {
            QTimer::singleShot(ms, [weak_self, t]() {
                auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                t();
            });
        });
    }

    std::shared_ptr<MessageNotifier> ClientContext::GetMessageNotifier() {
        return msg_notifier_;
    }

    std::shared_ptr<MessageListener> ClientContext::ObtainMessageListener() {
        return msg_notifier_->CreateListener(MessageExecutionLane::kControl);
    }

    std::shared_ptr<MessageListener> ClientContext::ObtainUIMessageListener() {
        auto weak_self = weak_from_this();
        return msg_notifier_->CreateListener(
            MessageExecutionLane::kUi, [weak_self](std::function<void()> task) {
            if (auto self = weak_self.lock(); self && !self->exiting_) {
                self->PostUITask(std::move(task));
            }
        });
    }

    void ClientContext::Exit() {
        bool expected = false;
        if (!exiting_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            return;
        }
        if (msg_notifier_) {
            msg_notifier_->Stop(MessageBusStopMode::kCancel);
        }
        if (task_thread_ && task_thread_->IsJoinable()) {
            task_thread_->Exit();
        }
    }

    void ClientContext::SaveKeyValue(const std::string& k, const std::string& v) {
        sp_->Put(k, v);
    }

    std::string ClientContext::GetValueByKey(const std::string& k) {
        return sp_->Get(k);
    }

    void ClientContext::UpdateCapturingMonitorInfo(const SdkCaptureMonitorInfo& info) {

        // save
        capturing_info_map_[info.mon_name_] = info;
    }

    std::map<std::string, SdkCaptureMonitorInfo> ClientContext::GetCapturingMonitorInfoMap() {
        return capturing_info_map_;
    }

    void ClientContext::SetPluginManager(const std::shared_ptr<ClientPluginManager>& mgr) {
        plugin_mgr_ = mgr;
    }

    std::shared_ptr<ClientPluginManager> ClientContext::GetPluginManager() {
        return plugin_mgr_;
    }

    void ClientContext::SetRecording(bool recording) {
        recording_ = recording;
    }

    bool ClientContext::GetRecording() {
        return recording_;
    }

    void ClientContext::InitNotifyManager(QWidget* parent) {
        notify_manager_ = std::make_shared<NotifyManager>(parent);
        const auto weak_self = weak_from_this();
        connect(notify_manager_.get(), &NotifyManager::notifyDetail, this,
                [weak_self](const NotifyItem& data) {
            if (auto self = weak_self.lock()) {
                self->PostTask([weak_self, data]() {
                    auto task_self = weak_self.lock();
                    if (!task_self) {
                        return;
                    }
                    task_self->SendAppMessage(MsgClientNotificationClicked {
                    .data_ = data,
                    });
                });
            }
        });
    }

    std::shared_ptr<NotifyManager> ClientContext::GetNotifyManager() const {
        return notify_manager_;
    }

    void ClientContext::NotifyAppMessage(const QString& title, const QString& msg, std::function<void()>&& cbk) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, title, msg, cbk = std::move(cbk)]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (self->notify_manager_) {
                self->notify_manager_->notify(NotifyItem {
                    .type_ = NotifyItemType::kNormal,
                    .title_ = title,
                    .body_ = msg,
                    .cbk_ = cbk,
                });
            }
        });
    }

    void ClientContext::NotifyAppErrMessage(const QString& title, const QString& msg, std::function<void()>&& cbk) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, title, msg, cbk = std::move(cbk)]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (self->notify_manager_) {
                self->notify_manager_->notify(NotifyItem {
                    .type_ = NotifyItemType::kError,
                    .title_ = title,
                    .body_ = msg,
                    .cbk_ = cbk,
                });
            }
        });
    }

    void ClientContext::NotifyAppWarningMessage(const QString& title, const QString& msg, std::function<void()>&& cbk) {
        auto weak_self = weak_from_this();
        QMetaObject::invokeMethod(this, [weak_self, title, msg, cbk = std::move(cbk)]() {
            auto self = weak_self.lock();
            if (!self || self->exiting_) {
                return;
            }
            if (self->notify_manager_) {
                self->notify_manager_->notify(NotifyItem{
                    .type_ = NotifyItemType::kWarning,
                    .title_ = title,
                    .body_ = msg,
                    .cbk_ = cbk,
                });
            }
        });
    }
}
