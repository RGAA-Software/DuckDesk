//
// ft 主控端插件壳 — rustdesk 协议迁移阶段 3
//

#include "ft_client_plugin.h"
#include "ft_core.h"
#include "ui/ft_window.h"

#include <atomic>
#include <format>
#include <mutex>

#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "ft_terminal.h"
#include "px_client/plugin_interface/ct_plugin_ids.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "no_margin_layout.h"
#include "translator/px_translator.h"
#include "widget_helper.h"

PX_PLUGIN_EXPORT(px::FtClientPlugin)

namespace px
{

    class FtClientTransportState final {
    public:
        FtClientTransportState(
            const ClientPluginSettings& settings,
            ClientPluginEventCallback event_dispatcher)
            : event_dispatcher_(std::move(event_dispatcher)) {
            UpdateSettings(settings);
        }

        void UpdateSettings(const ClientPluginSettings& settings) {
            std::lock_guard lock(mutex_);
            device_id_ = settings.device_id_;
            stream_id_ = settings.stream_id_;
        }

        FileTransferSendResult Send(const px::Message& message) const {
            px::Message output = message;
            if (output.has_file_response()) {
                output.set_type(MessageType::kFileResponse);
            }
            else if (output.has_file_action()) {
                output.set_type(MessageType::kFileAction);
            }
            {
                std::lock_guard lock(mutex_);
                output.set_stream_id(stream_id_);
                output.set_device_id(device_id_);
            }

            auto event = std::make_shared<ClientPluginNetworkEvent>();
            event->media_channel_ = false;
            event->buf_ = px::ProtoAsData(&output);
            event_dispatcher_(event);
            if (!event->send_result_.accepted()) {
                static std::atomic<std::uint64_t> last_deferred_log_ms{0};
                const auto now = TimeUtil::GetCurrentTimestamp();
                auto previous =
                    last_deferred_log_ms.load(std::memory_order_relaxed);
                if (now - previous >= 10000 &&
                    last_deferred_log_ms.compare_exchange_strong(
                        previous, now, std::memory_order_relaxed)) {
                    LOGW("File-transfer send deferred, status: {}, detail: {}",
                         static_cast<int>(event->send_result_.status()),
                         event->send_result_.detail());
                }
            }
            return event->send_result_;
        }

    private:
        mutable std::mutex mutex_;
        std::string device_id_;
        std::string stream_id_;
        ClientPluginEventCallback event_dispatcher_;
    };

    std::string FtClientPlugin::GetPluginId() {
        return kClientFtPluginId;
    }

    std::string FtClientPlugin::GetPluginName() {
        return "File Transfer";
    }

    std::string FtClientPlugin::GetVersionName() {
        return "1.0.0";
    }

    uint32_t FtClientPlugin::GetVersionCode() {
        return 100;
    }

    std::string FtClientPlugin::GetPluginDescription() {
        return "File transfer (rustdesk protocol, px_ft_engine core)";
    }

    bool FtClientPlugin::OnCreate(const px::ClientPluginParam& param) {
        ClientPluginInterface::OnCreate(param);
        plugin_type_ = ClientPluginType::kUtil;

        if (!IsPluginEnabled()) {
            return true;
        }

        // px_qt_widget 是静态库,插件 dll 内有独立的 TcTranslatorManager 实例,
        // 宿主进程已加载的语言表不会带过来,必须在插件内自行初始化。
        tcTrMgr()->InitLanguage();

        // core:引擎薄适配层(worker 线程模型,见 ft_core.h)
        transport_state_ = std::make_shared<FtClientTransportState>(
            plugin_settings_, MakeDirectEventDispatcher());
        const auto weak_transport =
            std::weak_ptr<FtClientTransportState>(transport_state_);
        core_ = std::make_unique<FtCore>(
            [weak_transport](const Message& message) {
                if (const auto transport = weak_transport.lock()) {
                    return transport->Send(message);
                }
                return FileTransferSendResult::Disconnected(
                    "FT client transport was destroyed");
            });
        core_->Start();

        // UI:三栏文件管理窗口
        root_widget_->resize(1280, 760);
        root_widget_->hide();
        auto window =
            std::make_unique<FtWindow>(core_.get(), root_widget_.get());
        window_ = window.get();
        auto layout = std::make_unique<NoMarginHLayout>();
        layout->addWidget(window.release());
        root_widget_->setLayout(layout.release());
        WidgetHelper::SetTitleBarColor(root_widget_.get());
        const QString remote_name = !plugin_settings_.stream_name_.empty()
            ? QString::fromStdString(plugin_settings_.stream_name_)
            : QString::fromStdString(plugin_settings_.device_id_);
        root_widget_->setWindowTitle(
            QString("%1 [%2]").arg(tcTr("id_file_transfer"), remote_name));
        // 远程栏标题显示对端标识(远端: xxx)
        window_->SetRemoteDeviceName(remote_name);

        // 审计:对接 Console 传输记录链路(旧插件同款事件)
        connect(core_.get(), &FtCore::SigJobAdded, this,
                &FtClientPlugin::TrackJobBegin);
        connect(core_.get(), &FtCore::SigJobDone, this,
                &FtClientPlugin::TrackJobEnd);

        LOGI("ft client plugin created.");
        return true;
    }

    bool FtClientPlugin::OnStop() {
        if (core_) {
            core_->Stop();
        }
        return ClientPluginInterface::OnStop();
    }

    bool FtClientPlugin::OnDestroy() {
        if (core_) {
            core_->Stop();
        }
        window_ = nullptr;
        const auto destroyed = ClientPluginInterface::OnDestroy();
        core_.reset();
        transport_state_.reset();
        return destroyed;
    }

    void FtClientPlugin::OnMessage(std::shared_ptr<Message> msg) {
        ClientPluginInterface::OnMessage(msg);
        const auto type = msg->type();
        if (type != MessageType::kFileAction && type != MessageType::kFileResponse) {
            return;
        }
        if (core_) {
            core_->EnqueueMessage(msg);
        }
    }

    void FtClientPlugin::ShowRootWidget() {
        ClientPluginInterface::ShowRootWidget();
        if (window_) {
            window_->OnShow();
        }
        root_widget_->raise();
        root_widget_->activateWindow();
        root_widget_->showNormal();
    }

    bool FtClientPlugin::HasProcessingTasks() {
        return core_ && core_->HasJobs();
    }

    void FtClientPlugin::OnTransportConnected() {
        // A file-only window is shown before networking starts, and a normal
        // client's window may also be opened while that client is connecting.
        // Retry the remote root only after the FT transport is actually ready.
        // Do not raise a hidden/closed window during an unrelated reconnect.
        if (window_ && root_widget_ && root_widget_->isVisible()) {
            window_->OnTransportConnected();
        }
    }

    void FtClientPlugin::SyncClientPluginSettings(const px::ClientPluginSettings& st) {
        ClientPluginInterface::SyncClientPluginSettings(st);
        plugin_settings_.max_transmit_speed_ = st.max_transmit_speed_;
        if (transport_state_) {
            transport_state_->UpdateSettings(plugin_settings_);
        }
        if (core_) {
            // max_transmit_speed_ 为 bit/s,引擎限速按 byte/s
            core_->SetRateLimitBytesPerSec(st.max_transmit_speed_ / 8);
        }
    }

    void FtClientPlugin::TrackJobBegin(int32_t job_id, const QString& name, bool is_download) {
        audit_jobs_[job_id] = name;
        auto event = std::make_shared<ClientPluginFileTransferBeginEvent>();
        // router 会对 task_id_ 再做 MD5;带上作业 id 避免同名文件撞记录
        event->task_id_ = std::format("{}#{}", name.toStdString(), job_id);
        event->file_path_ = name.toStdString();
        event->direction_ = is_download ? "In" : "Out";
        CallbackEvent(event);
    }

    void FtClientPlugin::TrackJobEnd(int32_t job_id, const QString& error_or_empty) {
        auto it = audit_jobs_.find(job_id);
        if (it == audit_jobs_.end()) {
            return;
        }
        auto event = std::make_shared<ClientPluginFileTransferEndEvent>();
        event->task_id_ = std::format("{}#{}", it->second.toStdString(), job_id);
        event->file_path_ = it->second.toStdString();
        event->direction_ = ""; // router 不回填 direction,Begin 已定
        const auto terminal = px::ft::ClassifyTerminal(error_or_empty.toStdString());
        event->success_ = terminal.success;
        event->status_ = terminal.status;
        event->end_reason_ = terminal.reason;
        CallbackEvent(event);
        audit_jobs_.erase(it);
    }

}
