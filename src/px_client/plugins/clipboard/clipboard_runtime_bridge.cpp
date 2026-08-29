#include "clipboard_runtime_bridge.h"

#include "px_common_new/file.h"
#include "px_common_new/md5.h"
#include "px_message.pb.h"
#include "px_message_new/proto_converter.h"
#include "px_client/plugin_interface/ct_plugin_events.h"
#include "win/cp_file_struct.h"

namespace px {

ClipboardRuntimeBridge::ClipboardRuntimeBridge(
    const ClientPluginSettings& settings,
    ClientPluginEventCallback event_dispatcher)
    : settings_(settings), event_dispatcher_(std::move(event_dispatcher)) {
}

void ClipboardRuntimeBridge::UpdateSettings(
    const ClientPluginSettings& settings) {
    std::lock_guard lock(mutex_);
    settings_ = settings;
}

void ClipboardRuntimeBridge::Deactivate() {
    lifetime_token_->store(false);
}

bool ClipboardRuntimeBridge::IsEnabled() const {
    return lifetime_token_->load() && SettingsSnapshot().clipboard_enabled_;
}

const std::shared_ptr<std::atomic_bool>&
ClipboardRuntimeBridge::LifetimeToken() const {
    return lifetime_token_;
}

void ClipboardRuntimeBridge::Dispatch(
    const std::shared_ptr<ClientPluginBaseEvent>& event) const {
    if (lifetime_token_->load() && event_dispatcher_) {
        event_dispatcher_(event);
    }
}

ClientPluginSettings ClipboardRuntimeBridge::SettingsSnapshot() const {
    std::lock_guard lock(mutex_);
    return settings_;
}

bool ClipboardRuntimeBridge::RequestBuffer(
    const ClipboardFileWrapper& file_wrapper,
    int64_t request_index,
    int64_t request_start,
    unsigned long request_size) const {
    if (!lifetime_token_->load()) {
        return false;
    }
    const auto settings = SettingsSnapshot();
    px::Message message;
    message.set_device_id(settings.device_id_);
    message.set_stream_id(settings.stream_id_);
    message.set_type(MessageType::kClipboardReqBuffer);
    auto request = message.mutable_cp_req_buffer();
    request->set_req_index(request_index);
    request->set_req_size(request_size);
    request->set_req_start(request_start);
    request->set_full_name(file_wrapper.file_.full_path());

    auto event = std::make_shared<ClientPluginNetworkEvent>();
    event->media_channel_ = false;
    event->buf_ = px::ProtoAsData(&message);
    Dispatch(event);
    return true;
}

void ClipboardRuntimeBridge::OnRequestFileBegin(
    const std::shared_ptr<Message>& message) const {
    if (!message) {
        return;
    }
    const auto request = message->cp_req_at_begin();
    auto event = std::make_shared<ClientPluginFileTransferBeginEvent>();
    event->task_id_ = MD5::Hex(request.full_name());
    event->file_path_ = request.full_name();
    event->direction_ = "Out";
    Dispatch(event);
}

void ClipboardRuntimeBridge::OnRequestFileBuffer(
    const std::shared_ptr<Message>& message) const {
    if (!message) {
        return;
    }
    const auto& request = message->cp_req_buffer();
    auto file = File::OpenForReadB(U8Path(request.full_name()));
    DataPtr data;
    if (file->Exists()) {
        uint64_t read_size = 0;
        data = file->Read(
            request.req_start(), request.req_size(), read_size);
    }

    const auto settings = SettingsSnapshot();
    px::Message response;
    response.set_device_id(settings.device_id_);
    response.set_stream_id(settings.stream_id_);
    response.set_type(MessageType::kClipboardRespBuffer);
    auto buffer = response.mutable_cp_resp_buffer();
    buffer->set_full_name(request.full_name());
    buffer->set_req_size(request.req_size());
    buffer->set_req_start(request.req_start());
    buffer->set_req_index(request.req_index());
    if (data) {
        buffer->set_read_size(data->Size());
        buffer->set_buffer(data->AsString());
    }

    auto event = std::make_shared<ClientPluginNetworkEvent>();
    event->media_channel_ = false;
    event->buf_ = px::ProtoAsData(&response);
    Dispatch(event);
}

void ClipboardRuntimeBridge::OnRequestFileEnd(
    const std::shared_ptr<Message>& message) const {
    if (!message) {
        return;
    }
    const auto request = message->cp_req_at_end();
    auto event = std::make_shared<ClientPluginFileTransferEndEvent>();
    event->task_id_ = MD5::Hex(request.full_name());
    event->file_path_ = request.full_name();
    event->direction_ = "Out";
    event->success_ = request.success();
    Dispatch(event);
}

}  // namespace px
