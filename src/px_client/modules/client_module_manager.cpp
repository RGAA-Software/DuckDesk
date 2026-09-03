#include "client_module_manager.h"

#include "ct_app_message.h"
#include "ct_base_workspace.h"
#include "ct_client_context.h"
#include "ct_settings.h"
#include "px_client/modules/clipboard/clipboard_module.h"
#include "px_client/modules/file_transfer/file_transfer_module.h"
#include "px_client/modules/media_recording/media_recording_module.h"
#include "px_client_sdk_new/thunder_sdk.h"
#include "px_common_new/file.h"
#include "px_common_new/folder_util.h"
#include "px_common_new/log.h"
#include "px_common_new/md5.h"
#include "px_common_new/time_util.h"
#include "px_message_new/proto_converter.h"

namespace px {

std::shared_ptr<ClientModuleManager> ClientModuleManager::Make(
    const std::shared_ptr<BaseWorkspace>& workspace) {
    return std::make_shared<ClientModuleManager>(workspace);
}

ClientModuleManager::ClientModuleManager(
    const std::shared_ptr<BaseWorkspace>& workspace)
    : workspace_(workspace), context_(workspace->GetContext()) {
}

ClientModuleManager::~ClientModuleManager() {
    Stop();
}

ClientModuleConfig ClientModuleManager::BuildConfig() const {
    const auto& settings = *Settings::Instance();
    return ClientModuleConfig{
        .screen_recording_path_ = settings.screen_recording_path_,
        .settings_ = {
            .clipboard_enabled_ = settings.clipboard_on_,
            .device_id_ = settings.device_id_.empty()
                ? settings.my_host_ : settings.device_id_,
            .stream_id_ = settings.stream_id_,
            .language_ = static_cast<int>(settings.language_),
            .stream_name_ = settings.stream_name_,
            .display_name_ = settings.display_name_,
            .display_remote_name_ = settings.display_remote_name_,
            .max_transmit_speed_ = settings.max_transmit_speed_,
            .max_receive_speed_ = settings.max_receive_speed_,
        },
    };
}

bool ClientModuleManager::Start() {
    if (!stopping_.exchange(false)) {
        return true;
    }

    const auto config = BuildConfig();
    const std::shared_ptr<ClientModuleServices> services = shared_from_this();
    const std::weak_ptr<ClientModuleServices> weak_services = services;

    auto file_transfer = std::make_shared<ClientFileTransferModule>(weak_services);
    if (!file_transfer->Start(config)) {
        LOGE("Failed to start built-in Client file-transfer module");
        file_transfer.reset();
    }

    std::shared_ptr<ClientClipboardModule> clipboard;
    std::shared_ptr<ClientMediaRecordingModule> media_recording;
    if (!Settings::Instance()->file_transfer_only_) {
        clipboard = std::make_shared<ClientClipboardModule>(weak_services);
        if (!clipboard->Start(config)) {
            LOGE("Failed to start built-in Client clipboard module");
            clipboard.reset();
        }

        media_recording =
            std::make_shared<ClientMediaRecordingModule>(weak_services);
        if (!media_recording->Start(config)) {
            LOGE("Failed to start built-in Client media-recording module");
            media_recording.reset();
        }
    }

    {
        std::lock_guard lock(modules_mutex_);
        file_transfer_ = std::move(file_transfer);
        clipboard_ = std::move(clipboard);
        media_recording_ = std::move(media_recording);
    }

    LOGI("Built-in Client modules started: clipboard={}, file_transfer={}, recording={}",
         static_cast<bool>(GetClipboardModule()),
         static_cast<bool>(GetFileTransferModule()),
         static_cast<bool>(GetMediaRecordingModule()));
    return static_cast<bool>(GetFileTransferModule());
}

void ClientModuleManager::Stop() {
    if (stopping_.exchange(true)) {
        return;
    }

    std::shared_ptr<ClientClipboardModule> clipboard;
    std::shared_ptr<ClientFileTransferModule> file_transfer;
    std::shared_ptr<ClientMediaRecordingModule> media_recording;
    {
        std::lock_guard lock(modules_mutex_);
        clipboard = std::move(clipboard_);
        file_transfer = std::move(file_transfer_);
        media_recording = std::move(media_recording_);
    }
    if (clipboard) {
        clipboard->Stop();
    }
    if (file_transfer) {
        file_transfer->Stop();
    }
    if (media_recording) {
        media_recording->Stop();
    }
}

void ClientModuleManager::HandleMessage(
    const std::shared_ptr<Message>& message) {
    if (!message || stopping_) {
        return;
    }
    switch (message->type()) {
    case MessageType::kVideoFrame:
    case MessageType::kAudioFrame:
        if (const auto module = GetMediaRecordingModule()) {
            module->HandleMessage(message);
        }
        break;
    case MessageType::kClipboardInfo:
    case MessageType::kClipboardInfoResp:
    case MessageType::kClipboardReqAtBegin:
    case MessageType::kClipboardReqBuffer:
    case MessageType::kClipboardReqAtEnd:
    case MessageType::kClipboardRespBuffer:
        if (const auto module = GetClipboardModule()) {
            module->HandleMessage(message);
        }
        break;
    case MessageType::kFileAction:
    case MessageType::kFileResponse:
        if (const auto module = GetFileTransferModule()) {
            module->HandleMessage(message);
        }
        break;
    default:
        break;
    }
}

void ClientModuleManager::UpdateSettings(
    const ClientModuleSettings& settings) {
    if (const auto module = GetClipboardModule()) {
        module->UpdateSettings(settings);
    }
    if (const auto module = GetFileTransferModule()) {
        module->UpdateSettings(settings);
    }
    if (const auto module = GetMediaRecordingModule()) {
        module->UpdateSettings(settings);
    }
}

void ClientModuleManager::OnTransportConnected() {
    if (const auto module = GetFileTransferModule()) {
        module->OnTransportConnected();
    }
}

std::shared_ptr<ClientClipboardModule>
ClientModuleManager::GetClipboardModule() const {
    std::lock_guard lock(modules_mutex_);
    return clipboard_;
}

std::shared_ptr<ClientFileTransferModule>
ClientModuleManager::GetFileTransferModule() const {
    std::lock_guard lock(modules_mutex_);
    return file_transfer_;
}

std::shared_ptr<ClientMediaRecordingModule>
ClientModuleManager::GetMediaRecordingModule() const {
    std::lock_guard lock(modules_mutex_);
    return media_recording_;
}

void ClientModuleManager::SendClipboardUpdate(
    ClipboardType type,
    std::string text,
    std::vector<ClipboardFile> files) {
    if (const auto context = context_.lock()) {
        context->SendAppMessage(MsgClientClipboard{
            .type_ = type,
            .msg_ = std::move(text),
            .files_ = std::move(files),
        });
    }
}

void ClientModuleManager::SendRemoteClipboardResponse(
    std::string remote_text) {
    const auto workspace = workspace_.lock();
    if (!workspace) {
        return;
    }
    const auto sdk = workspace->GetThunderSdk();
    if (!sdk) {
        LOGW("Clipboard response ignored before Client transport initialization");
        return;
    }
    const auto& settings = *Settings::Instance();
    Message response;
    response.set_type(MessageType::kClipboardInfoResp);
    response.set_device_id(settings.device_id_);
    response.set_stream_id(settings.stream_id_);
    auto& clipboard = *response.mutable_clipboard_info_resp();
    clipboard.set_type(ClipboardType::kClipboardText);
    clipboard.set_msg(std::move(remote_text));
    sdk->PostMediaMessage(ProtoAsData(&response));
}

void ClientModuleManager::PostMediaMessage(std::shared_ptr<Data> data) {
    if (const auto workspace = workspace_.lock(); workspace && data) {
        if (const auto sdk = workspace->GetThunderSdk()) {
            sdk->PostMediaMessage(std::move(data));
        }
    }
}

FileTransferSendResult ClientModuleManager::PostFileTransferMessage(
    std::shared_ptr<Data> data) {
    const auto workspace = workspace_.lock();
    const auto sdk = workspace ? workspace->GetThunderSdk() : nullptr;
    if (!sdk || !data) {
        return FileTransferSendResult::Disconnected(
            "Client module transport is unavailable");
    }
    return sdk->PostFileTransferMessage(std::move(data));
}

void ClientModuleManager::ReportFileTransferBegin(
    std::string task_id,
    std::string file_path,
    std::string direction) {
    const auto context = context_.lock();
    if (!context) {
        return;
    }
    const auto& settings = *Settings::Instance();
    context->SendAppMessage(MsgClientFileTransmissionBegin{
        .the_file_id_ = MD5::Hex(task_id),
        .begin_timestamp_ = static_cast<std::int64_t>(
            TimeUtil::GetCurrentTimestamp()),
        .direction_ = std::move(direction),
        .file_detail_ = std::move(file_path),
        .remote_device_id_ = settings.remote_device_id_.empty()
            ? settings.host_ : settings.remote_device_id_,
    });
}

void ClientModuleManager::ReportFileTransferEnd(
    std::string task_id,
    bool success,
    std::string status,
    std::string reason) {
    if (const auto context = context_.lock()) {
        context->SendAppMessage(MsgClientFileTransmissionEnd{
            .the_file_id_ = MD5::Hex(task_id),
            .end_timestamp_ = static_cast<std::int64_t>(
                TimeUtil::GetCurrentTimestamp()),
            .duration_ = 0,
            .success_ = success,
            .status_ = std::move(status),
            .end_reason_ = std::move(reason),
        });
    }
}

void ClientModuleManager::NotifyRecordingComplete(std::string directory) {
    if (const auto context = context_.lock()) {
        const auto path = directory;
        context->NotifyAppMessage(
            "Screen recording success",
            QString::fromStdString(directory),
            [path]() {
                FolderUtil::OpenDir(PathFromUTF8(path));
            });
    }
}

}  // namespace px
