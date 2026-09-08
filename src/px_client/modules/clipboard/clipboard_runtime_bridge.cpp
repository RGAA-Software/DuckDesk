#include "clipboard_runtime_bridge.h"

#include <utility>

#include "px_client/modules/client_module_services.h"
#include "px_common/file.h"
#include "px_common/log.h"
#include "px_common/md5.h"
#include "px_common/path_codec.h"
#include "px_common/uuid.h"
#include "px_client_sdk/sdk_clipboard_protocol.h"
#include "px_message/proto_converter.h"
#include "win/cp_file_struct.h"

namespace px {

ClipboardRuntimeBridge::ClipboardRuntimeBridge(
    std::weak_ptr<ClientModuleServices> services)
    : services_(std::move(services)) {
}

void ClipboardRuntimeBridge::Activate(
    const ClientModuleSettings& settings) {
    UpdateSettings(settings);
    lifetime_token_->store(true);
}

void ClipboardRuntimeBridge::UpdateSettings(
    const ClientModuleSettings& settings) {
    std::lock_guard lock(mutex_);
    settings_ = settings;
    if (!settings_.clipboard_enabled_) {
        published_files_.clear();
    }
}

void ClipboardRuntimeBridge::Deactivate() {
    lifetime_token_->store(false);
    RevokeLocalFiles();
}

void ClipboardRuntimeBridge::RevokeLocalFiles() const {
    std::lock_guard lock(mutex_);
    published_files_.clear();
}

std::optional<ClipboardFile> ClipboardRuntimeBridge::PublishedFile(const std::string& transfer_name) const {
    std::lock_guard lock(mutex_);
    if (!lifetime_token_->load() || !settings_.clipboard_enabled_) {
        return {};
    }
    const auto file = published_files_.find(transfer_name);
    return file == published_files_.end() ? std::nullopt : std::optional(file->second);
}

bool ClipboardRuntimeBridge::IsEnabled() const {
    return lifetime_token_->load() && SettingsSnapshot().clipboard_enabled_;
}

const std::shared_ptr<std::atomic_bool>&
ClipboardRuntimeBridge::LifetimeToken() const {
    return lifetime_token_;
}

ClientModuleSettings ClipboardRuntimeBridge::SettingsSnapshot() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

void ClipboardRuntimeBridge::SendClipboardUpdate(
    ClipboardType type,
    std::string text,
    std::vector<ClipboardFile> files) const {
    if (!IsEnabled()) {
        return;
    }
    if (const auto services = services_.lock()) {
        std::unordered_map<std::string, ClipboardFile> offered{};
        const auto generation = GetUUID();
        if (type == kClipboardFiles) {
            for (std::size_t index{}; index < files.size(); ++index) {
                auto& file = files[index];
                if (!IsClipboardFileDescriptorValid(file.file_name(), file.full_path(), file.total_size())) {
                    RevokeLocalFiles();
                    return;
                }
                const auto token = "pixels-clipboard://" + generation + "/" + std::to_string(index);
                offered.emplace(token, file);
                file.set_full_path(token);
            }
        }
        {
            std::lock_guard lock(mutex_);
            if (!lifetime_token_->load() || !settings_.clipboard_enabled_) {
                return;
            }
            published_files_ = std::move(offered);
        }
        services->SendClipboardUpdate(type, std::move(text), std::move(files));
    }
}

void ClipboardRuntimeBridge::SendRemoteClipboardResponse(
    std::string remote_text) const {
    if (!lifetime_token_->load()) {
        return;
    }
    if (const auto services = services_.lock()) {
        services->SendRemoteClipboardResponse(std::move(remote_text));
    }
}

void ClipboardRuntimeBridge::PostMediaMessage(
    std::shared_ptr<Data> data) const {
    if (!lifetime_token_->load()) {
        return;
    }
    if (const auto services = services_.lock()) {
        services->PostMediaMessage(std::move(data));
    }
}

void ClipboardRuntimeBridge::ReportFileTransferBegin(
    std::string task_id,
    std::string file_path,
    std::string direction) const {
    if (!lifetime_token_->load()) {
        return;
    }
    if (const auto services = services_.lock()) {
        services->ReportFileTransferBegin(
            std::move(task_id), std::move(file_path), std::move(direction));
    }
}

void ClipboardRuntimeBridge::ReportFileTransferEnd(
    std::string task_id,
    bool success,
    std::string status,
    std::string reason) const {
    if (!lifetime_token_->load()) {
        return;
    }
    if (const auto services = services_.lock()) {
        services->ReportFileTransferEnd(
            std::move(task_id), success, std::move(status), std::move(reason));
    }
}

bool ClipboardRuntimeBridge::RequestBuffer(const ClipboardFileWrapper& file_wrapper, std::int64_t request_index, std::int64_t request_start,
                                           unsigned long request_size) const {
    if (!IsEnabled()) {
        return false;
    }
    const auto services = services_.lock();
    if (!services) {
        return false;
    }
    const auto settings = SettingsSnapshot();
    Message message;
    message.set_device_id(settings.device_id_);
    message.set_stream_id(settings.stream_id_);
    message.set_type(MessageType::kClipboardReqBuffer);
    auto& request = *message.mutable_cp_req_buffer();
    request.set_req_index(request_index);
    request.set_req_size(request_size);
    request.set_req_start(request_start);
    request.set_full_name(file_wrapper.file_.full_path());
    if (!ClipboardReadRequest::From(request).IsValid()) {
        return false;
    }
    return services->PostFileTransferMessage(ProtoAsData(&message)).accepted();
}

void ClipboardRuntimeBridge::OnRequestFileBegin(
    const std::shared_ptr<Message>& message) const {
    const auto services = services_.lock();
    if (!lifetime_token_->load() || !message || !services) {
        return;
    }
    const auto& request = message->cp_req_at_begin();
    const auto file = PublishedFile(request.full_name());
    if (!message->has_cp_req_at_begin() || !file) {
        return;
    }
    services->ReportFileTransferBegin(MD5::Hex(request.full_name()), file->full_path(), "Out");
}

void ClipboardRuntimeBridge::OnRequestFileBuffer(
    const std::shared_ptr<Message>& message) const {
    const auto services = services_.lock();
    if (!lifetime_token_->load() || !message || !services) {
        return;
    }
    const auto& request = message->cp_req_buffer();
    const auto published = PublishedFile(request.full_name());
    if (!message->has_cp_req_buffer() || !published) {
        return;
    }
    const auto read_size_limit = ClipboardReadRequest::From(request).ReadSize(published->total_size());
    if (!read_size_limit) {
        return;
    }
    DataPtr data{};
    const auto full_path = PathFromUtf8(published->full_path());
    if (!full_path) {
        LOGE("event=client.clipboard.invalid_file_path stage=decode_utf8 error={}", full_path.Error().message);
    } else {
        const auto file = File::OpenForReadB(full_path.Value());
        if (file && file->Exists() && *read_size_limit > 0) {
            std::uint64_t read_size{};
            data = file->Read(request.req_start(), *read_size_limit, read_size);
        }
    }

    if (!PublishedFile(request.full_name())) {
        return;
    }

    const auto settings = SettingsSnapshot();
    Message response;
    response.set_device_id(settings.device_id_);
    response.set_stream_id(settings.stream_id_);
    response.set_type(MessageType::kClipboardRespBuffer);
    auto& buffer = *response.mutable_cp_resp_buffer();
    buffer.set_full_name(request.full_name());
    buffer.set_req_size(request.req_size());
    buffer.set_req_start(request.req_start());
    buffer.set_req_index(request.req_index());
    if (data) {
        buffer.set_read_size(data->Size());
        buffer.set_buffer(data->AsString());
    }
    static_cast<void>(
        services->PostFileTransferMessage(ProtoAsData(&response)));
}

void ClipboardRuntimeBridge::OnRequestFileEnd(
    const std::shared_ptr<Message>& message) const {
    const auto services = services_.lock();
    if (!lifetime_token_->load() || !message || !services) {
        return;
    }
    const auto& request = message->cp_req_at_end();
    if (!message->has_cp_req_at_end() || !PublishedFile(request.full_name())) {
        return;
    }
    services->ReportFileTransferEnd(
        MD5::Hex(request.full_name()), request.success(),
        request.success() ? "success" : "failed",
        request.success() ? std::string{} : "clipboard file transfer failed");
}

}  // namespace px
