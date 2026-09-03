#include "file_transfer_module.h"

#include <format>
#include <mutex>
#include <utility>

#include <QHBoxLayout>
#include <QWidget>

#include "ft_core.h"
#include "ft_terminal.h"
#include "px_client/modules/client_module_services.h"
#include "px_common_new/log.h"
#include "px_common_new/time_util.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "translator/px_translator.h"
#include "ui/ft_window.h"
#include "widget_helper.h"

namespace {

void EnsureFileTransferResourcesRegistered() {
    static const bool registered = []() {
        Q_INIT_RESOURCE(ft_res);
        return true;
    }();
    static_cast<void>(registered);
}

}  // namespace

namespace px {

class FtClientTransportState final {
public:
    FtClientTransportState(
        const ClientModuleSettings& settings,
        std::weak_ptr<ClientModuleServices> services)
        : services_(std::move(services)) {
        UpdateSettings(settings);
    }

    void UpdateSettings(const ClientModuleSettings& settings) {
        std::lock_guard lock(mutex_);
        device_id_ = settings.device_id_;
        stream_id_ = settings.stream_id_;
    }

    [[nodiscard]] FileTransferSendResult Send(const Message& message) const {
        Message output = message;
        if (output.has_file_response()) {
            output.set_type(MessageType::kFileResponse);
        } else if (output.has_file_action()) {
            output.set_type(MessageType::kFileAction);
        }
        {
            std::lock_guard lock(mutex_);
            output.set_stream_id(stream_id_);
            output.set_device_id(device_id_);
        }

        const auto services = services_.lock();
        const auto result = services
            ? services->PostFileTransferMessage(ProtoAsData(&output))
            : FileTransferSendResult::Disconnected(
                  "Client file-transfer module was stopped");
        if (!result.accepted()) {
            static std::atomic<std::uint64_t> last_deferred_log_ms{0};
            const auto now = TimeUtil::GetCurrentTimestamp();
            auto previous =
                last_deferred_log_ms.load(std::memory_order_relaxed);
            if (now - previous >= 10000 &&
                last_deferred_log_ms.compare_exchange_strong(
                    previous, now, std::memory_order_relaxed)) {
                LOGW("File-transfer send deferred, status: {}, detail: {}",
                     static_cast<int>(result.status()), result.detail());
            }
        }
        return result;
    }

private:
    mutable std::mutex mutex_;
    std::string device_id_;
    std::string stream_id_;
    std::weak_ptr<ClientModuleServices> services_;
};

ClientFileTransferModule::ClientFileTransferModule(
    std::weak_ptr<ClientModuleServices> services)
    : services_(std::move(services)) {
}

ClientFileTransferModule::~ClientFileTransferModule() {
    Stop();
}

bool ClientFileTransferModule::Start(const ClientModuleConfig& config) {
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_) {
        return true;
    }
    stopped_ = false;
    EnsureFileTransferResourcesRegistered();

    transport_state_ = std::make_shared<FtClientTransportState>(
        config.settings_, services_);
    const std::weak_ptr<FtClientTransportState> weak_transport =
        transport_state_;
    core_ = std::make_shared<FtCore>(
        [weak_transport](const Message& message) {
            if (const auto transport = weak_transport.lock()) {
                return transport->Send(message);
            }
            return FileTransferSendResult::Disconnected(
                "Client file-transfer transport was stopped");
        });
    core_->Start();

    root_widget_ = std::make_unique<QWidget>(nullptr, Qt::Window);
    root_widget_->resize(1280, 760);
    root_widget_->hide();
    window_ = new FtWindow( // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it.
        core_, root_widget_.get());
    const QPointer<QHBoxLayout> layout =
        new QHBoxLayout(root_widget_.get()); // NOLINT(gammaray-raw-pointer-boundary) Qt parent owns it; QPointer observes it.
    layout->setSpacing(0);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(window_);
    WidgetHelper::SetTitleBarColor(root_widget_.get());
    const QString remote_name = !config.settings_.stream_name_.empty()
        ? QString::fromStdString(config.settings_.stream_name_)
        : QString::fromStdString(config.settings_.device_id_);
    root_widget_->setWindowTitle(
        QString("%1 [%2]").arg(tcTr("id_file_transfer"), remote_name));
    window_->SetRemoteDeviceName(remote_name);

    const auto weak_self = weak_from_this();
    QObject::connect(
        core_.get(), &FtCore::SigJobAdded, core_.get(),
        [weak_self](int job_id, const QString& name, bool is_download) {
            if (const auto self = weak_self.lock()) {
                self->TrackJobBegin(job_id, name, is_download);
            }
        });
    QObject::connect(
        core_.get(), &FtCore::SigJobDone, core_.get(),
        [weak_self](int job_id, const QString& error) {
            if (const auto self = weak_self.lock()) {
                self->TrackJobEnd(job_id, error);
            }
        });

    LOGI("Built-in Client file-transfer module started");
    return true;
}

void ClientFileTransferModule::Stop() {
    std::shared_ptr<FtCore> core;
    std::unique_ptr<QWidget> root_widget;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        core = std::move(core_);
        window_ = nullptr;
        root_widget = std::move(root_widget_);
        audit_jobs_.clear();
        transport_state_.reset();
    }
    if (core) {
        core->Stop();
    }
    if (root_widget) {
        root_widget->hide();
        root_widget->close();
    }
}

void ClientFileTransferModule::HandleMessage(
    const std::shared_ptr<Message>& message) {
    if (!message) {
        return;
    }
    std::shared_ptr<FtCore> core;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        core = core_;
    }
    if (core) {
        core->EnqueueMessage(message);
    }
}

void ClientFileTransferModule::ShowWindow() {
    std::lock_guard lock(lifecycle_mutex_);
    if (stopped_ || !root_widget_ || !window_) {
        return;
    }
    window_->OnShow();
    root_widget_->raise();
    root_widget_->activateWindow();
    root_widget_->showNormal();
}

bool ClientFileTransferModule::HasProcessingTasks() const {
    std::shared_ptr<FtCore> core;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return false;
        }
        core = core_;
    }
    return core && core->HasJobs();
}

void ClientFileTransferModule::OnTransportConnected() {
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_ && window_ && root_widget_ && root_widget_->isVisible()) {
        window_->OnTransportConnected();
    }
}

void ClientFileTransferModule::UpdateSettings(
    const ClientModuleSettings& settings) {
    std::shared_ptr<FtClientTransportState> transport_state;
    std::shared_ptr<FtCore> core;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        transport_state = transport_state_;
        core = core_;
    }
    if (transport_state) {
        transport_state->UpdateSettings(settings);
    }
    if (core) {
        core->SetRateLimitBytesPerSec(settings.max_transmit_speed_ / 8);
    }
}

void ClientFileTransferModule::TrackJobBegin(
    int job_id,
    const QString& name,
    bool is_download) {
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        audit_jobs_[job_id] = name;
    }
    if (const auto services = services_.lock()) {
        services->ReportFileTransferBegin(
            std::format("{}#{}", name.toStdString(), job_id),
            name.toStdString(), is_download ? "In" : "Out");
    }
}

void ClientFileTransferModule::TrackJobEnd(
    int job_id,
    const QString& error_or_empty) {
    std::string task_id;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        const auto entry = audit_jobs_.find(job_id);
        if (entry == audit_jobs_.end()) {
            return;
        }
        task_id = std::format(
            "{}#{}", entry->second.toStdString(), job_id);
        audit_jobs_.erase(entry);
    }
    const auto terminal = ft::ClassifyTerminal(error_or_empty.toStdString());
    if (const auto services = services_.lock()) {
        services->ReportFileTransferEnd(
            task_id, terminal.success, std::string(terminal.status),
            std::string(terminal.reason));
    }
}

}  // namespace px
