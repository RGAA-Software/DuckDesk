#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>

#include <QPointer>
#include <QString>

#include "px_client/modules/client_module_settings.h"

class QWidget;

namespace px {

class ClientModuleServices;
class FtCore;
class FtClientTransportState;
class FtWindow;
class Message;

class ClientFileTransferModule final
    : public std::enable_shared_from_this<ClientFileTransferModule> {
public:
    explicit ClientFileTransferModule(
        std::weak_ptr<ClientModuleServices> services);
    ~ClientFileTransferModule();

    ClientFileTransferModule(const ClientFileTransferModule&) = delete;
    ClientFileTransferModule& operator=(
        const ClientFileTransferModule&) = delete;

    bool Start(const ClientModuleConfig& config);
    void Stop();
    void HandleMessage(const std::shared_ptr<Message>& message);
    void ShowWindow();
    [[nodiscard]] bool HasProcessingTasks() const;
    void OnTransportConnected();
    void UpdateSettings(const ClientModuleSettings& settings);

private:
    void TrackJobBegin(int job_id, const QString& name, bool is_download);
    void TrackJobEnd(int job_id, const QString& error_or_empty);

    std::weak_ptr<ClientModuleServices> services_;
    std::shared_ptr<FtCore> core_;
    std::unique_ptr<QWidget> root_widget_;
    QPointer<FtWindow> window_;
    std::shared_ptr<FtClientTransportState> transport_state_;
    std::unordered_map<int, QString> audit_jobs_;
    mutable std::mutex lifecycle_mutex_;
    bool stopped_ = true;
};

}  // namespace px
