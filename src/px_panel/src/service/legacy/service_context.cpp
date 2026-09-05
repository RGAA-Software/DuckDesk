//
// Created by RGAA on 21/10/2024.
//
//
// Deprecated:
// Historical C++ service support code. The active implementation is Rust.

#include "service_context.h"
#include "service_messages.h"
#include <filesystem>
#include "px_common_new/log.h"
#include "px_common_new/shared_preference.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/file_util.h"

namespace px
{
    ServiceContext::ServiceContext(int port) {
        listening_port_ = port;
        // message notifier
        msg_notifier_ = std::make_shared<MessageNotifier>();

        // pool
        iopool_ = std::make_shared<asio2::iopool>();
        iopool_->start();

        // shared preference
        sp_ = SharedPreference::Instance();
        auto exe_path = QString::fromStdWString(FolderUtil::GetCurrentFilePath()).toStdString();
        auto folder_path = QString::fromStdWString(FolderUtil::GetProgramDataPath()) + "/px_data";
        LOGI("Folder path: {}", folder_path.toStdString());
        if (!sp_->Init(std::filesystem::path{folder_path.toStdWString()}, "pixels_service.dat")) {
            LOGE("Init gammaray_service.data failed!");
        }

    }

    ServiceContext::~ServiceContext() {
        Exit();
    }

    void ServiceContext::Start() {
        if (started_.exchange(true) || exiting_) {
            return;
        }
        // timers
        timer_ = std::make_shared<asio2::timer>();
        std::vector<int> time_durations = {
            1000, 3000,
        };
        const std::weak_ptr<ServiceContext> weak_self = weak_from_this();
        for (auto& duration : time_durations) {
            timer_->start_timer(std::format("tid:{}", duration), duration, [weak_self, duration]() {
                const auto self = weak_self.lock();
                if (!self || self->exiting_) {
                    return;
                }
                if (duration == 1000) {
                    self->SendAppMessage(MsgTimer1S{});
                }
                else if (duration == 3000) {
                    self->SendAppMessage(MsgTimer3S{});
                }
            });
        }
    }

    void ServiceContext::Exit() {
        if (exiting_.exchange(true)) {
            return;
        }
        if (timer_) {
            timer_->stop_all_timers();
            timer_->stop();
        }
        if (iopool_) {
            iopool_->stop();
        }
        if (msg_notifier_) {
            msg_notifier_->Stop(MessageBusStopMode::kCancel);
        }
    }

    void ServiceContext::PostBgTask(std::function<void()>&& task) {
        if (!exiting_ && iopool_) {
            iopool_->post(std::move(task));
        }
    }

    std::shared_ptr<MessageListener> ServiceContext::CreateMessageListener() {
        return msg_notifier_->CreateListener();
    }

    std::string ServiceContext::GetAppExeFolderPath() {
        auto exe_path = QString::fromStdWString(FolderUtil::GetCurrentFilePath()).toStdString();
        return FileUtil::GetFileFolder(exe_path);
    }

}
