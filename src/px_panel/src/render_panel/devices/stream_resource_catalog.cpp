#include "stream_resource_catalog.h"

#include "connection_policy.h"
#include "stream_resource_refresh_gate.h"

#include "render_panel/database/stream_db_operator.h"
#include "render_panel/px_context.h"
#include "render_panel/user/px_user_manager.h"

#include "px_common/log.h"
#include "px_console_client/console_device.h"
#include "px_console_client/console_user_app_api.h"
#include "px_console_client/console_user_device.h"

#include <QUrl>

#include <unordered_set>
#include <utility>

namespace px {
namespace {

void RemoveIdentityDevices(const std::shared_ptr<StreamDBOperator>& database) {
    for (const auto& stream : database->GetAllStreamsSortByCreatedTime()) {
        if (stream && stream->connect_type_ == connection_policy::kConsoleDeviceTicket) database->DeleteStream(stream->_id);
    }
}

void RefreshDevices(const std::shared_ptr<PxUserManager>& users, const std::shared_ptr<StreamDBOperator>& database,
                    StreamResourceSnapshot& output) {
    const auto result = users->QueryBindDevices(1, 200, false);
    if (!result) {
        LOGW("Keep current Console devices because refresh failed: {}", static_cast<int>(result.error()));
        return;
    }
    std::unordered_set<std::string> availableIds{};
    ConsoleDeviceOnlineStates onlineStates{};
    for (const auto& binding : result.value()) {
        if (binding && binding->device_ && !binding->device_id_.empty()) {
            availableIds.insert(binding->device_id_);
            onlineStates[binding->device_id_] = binding->device_->active_;
        }
    }
    for (const auto& stream : database->GetAllStreamsSortByCreatedTime()) {
        if (stream && stream->connect_type_ == connection_policy::kConsoleDeviceTicket &&
            !availableIds.contains(stream->remote_device_id_)) {
            database->DeleteStream(stream->_id);
        }
    }
    for (const auto& binding : result.value()) {
        if (!binding || !binding->device_ || binding->device_id_.empty()) continue;
        const auto existing = database->GetStreamByRemoteDeviceId(binding->device_id_);
        const auto stream = existing.value_or(std::make_shared<px_console::ConsoleStream>());
        stream->remote_device_id_ = binding->device_id_;
        stream->stream_name_ = binding->device_->device_name_;
        stream->remote_device_random_pwd_.clear();
        stream->remote_device_safety_pwd_.clear();
        stream->stream_host_.clear();
        stream->stream_port_ = 0;
        stream->relay_host_.clear();
        stream->relay_port_ = 0;
        stream->connect_type_ = connection_policy::kConsoleDeviceTicket;
        stream->console_online_ = binding->device_->active_;
        if (existing) {
            database->UpdateStream(stream);
        } else {
            stream->clipboard_enabled_ = false;
            stream->audio_enabled_ = false;
            database->AddStream(stream);
        }
    }
    output.onlineStates = std::move(onlineStates);
}

void RefreshApplications(const std::shared_ptr<PxUserManager>& users, const std::string& host, const int port,
                         StreamResourceSnapshot& output) {
    const auto result = users->QueryApps();
    if (!result) {
        LOGW("Keep current Console applications because refresh failed: {}", static_cast<int>(result.error()));
        return;
    }
    std::vector<std::shared_ptr<px_console::ConsoleStream>> streams{};
    for (const auto& application : result.value()) {
        const auto stream = std::make_shared<px_console::ConsoleStream>();
        stream->stream_id_ = "console-app-" + application.app_id;
        stream->stream_name_ = application.name;
        stream->connect_type_ = connection_policy::kConsoleAppTicket;
        stream->console_app_id_ = application.app_id;
        stream->rdp_mode_ = application.app_type == "rdp";
        stream->console_access_mode_ = application.access_mode;
        stream->console_instance_state_ = "stopped";
        if (!application.cover_url.empty()) {
            QUrl cover{QString::fromStdString(application.cover_url)};
            if (cover.isRelative()) {
                QUrl base{};
                base.setScheme("https");
                base.setHost(QString::fromStdString(host));
                base.setPort(port);
                base.setPath("/");
                cover = base.resolved(cover);
            }
            stream->console_cover_url_ = cover.toString().toStdString();
        }
        stream->console_online_ = true;
        if (application.running_instance) {
            stream->console_instance_id_ = application.running_instance->instance_id;
            stream->console_instance_state_ = application.running_instance->state;
            stream->direct_online_ = application.running_instance->state == "running";
        }
        streams.push_back(std::move(stream));
    }
    output.applicationStreams = std::move(streams);
}

} // namespace

std::shared_ptr<StreamResourceCatalog> StreamResourceCatalog::Create(std::shared_ptr<PxContext> context,
                                                                     std::shared_ptr<StreamDBOperator> database,
                                                                     std::shared_ptr<PxUserManager> users,
                                                                     const StreamCatalogMode mode, std::string consoleHost,
                                                                     const int consolePort) {
    return std::make_shared<StreamResourceCatalog>(std::move(context), std::move(database), std::move(users), mode,
                                                   std::move(consoleHost), consolePort);
}

StreamResourceCatalog::StreamResourceCatalog(std::shared_ptr<PxContext> context, std::shared_ptr<StreamDBOperator> database,
                                             std::shared_ptr<PxUserManager> users, const StreamCatalogMode mode,
                                             std::string consoleHost, const int consolePort)
    : context_{std::move(context)}, database_{std::move(database)}, users_{std::move(users)}, gate_{StreamResourceRefreshGate::Create()},
      mode_{mode}, consoleHost_{std::move(consoleHost)}, consolePort_{consolePort} {}

StreamResourceCatalog::~StreamResourceCatalog() { Stop(); }

void StreamResourceCatalog::Stop() {
    if (gate_) gate_->Stop();
}

void StreamResourceCatalog::Refresh(const bool identityChanged, StreamResourceCompletion completion) {
    const auto generation = gate_->Begin(identityChanged);
    if (!generation) return;
    const auto context = context_;
    const auto database = database_;
    const auto users = users_;
    const auto gate = gate_;
    const auto mode = mode_;
    const auto host = consoleHost_;
    const int port = consolePort_;
    context_->PostNetworkTask([context, database, users, gate, mode, identityChanged, generation = *generation, host, port,
                               completion = std::move(completion)]() mutable {
        auto snapshot = std::make_shared<StreamResourceSnapshot>();
        static_cast<void>(gate->RunIfCurrent(generation, [database, users, mode, identityChanged, host, port, snapshot] {
            if (identityChanged && mode == StreamCatalogMode::RemoteDevices) RemoveIdentityDevices(database);
            if (mode == StreamCatalogMode::CloudApplications) RefreshApplications(users, host, port, *snapshot);
            else RefreshDevices(users, database, *snapshot);
        }));
        context->PostUITask([gate, generation, snapshot, completion = std::move(completion)]() mutable {
            if (gate->Complete(generation)) completion(std::move(*snapshot));
        });
    });
}

} // namespace px
