#include "clipboard_module.h"

#include <utility>

#include "clipboard_runtime_bridge.h"
#include "ct_clipboard_manager.h"
#include "px_client/modules/client_module_context.h"
#include "px_common/log.h"
#include "px_message.pb.h"

namespace px {

ClientClipboardModule::ClientClipboardModule(
    std::weak_ptr<ClientModuleServices> services)
    : runtime_bridge_(
          std::make_shared<ClipboardRuntimeBridge>(std::move(services))) {
}

ClientClipboardModule::~ClientClipboardModule() {
    Stop();
}

bool ClientClipboardModule::Start(const ClientModuleConfig& config) {
    std::lock_guard lock(lifecycle_mutex_);
    if (!stopped_) {
        return true;
    }
    stopped_ = false;
    context_ = std::make_shared<ClientModuleContext>("client.clipboard");
    runtime_bridge_->Activate(config.settings_);
    clipboard_manager_ = std::make_shared<ClipboardManager>(runtime_bridge_);
    if (!clipboard_manager_->Start()) {
        clipboard_manager_.reset();
        runtime_bridge_->Deactivate();
        context_->Stop();
        context_.reset();
        stopped_ = true;
        return false;
    }
    LOGI("Built-in Client clipboard module started");
    return true;
}

void ClientClipboardModule::Stop() {
    std::shared_ptr<ClientModuleContext> context;
    std::shared_ptr<ClipboardManager> clipboard_manager;
    {
        std::lock_guard lock(lifecycle_mutex_);
        if (stopped_) {
            return;
        }
        stopped_ = true;
        runtime_bridge_->Deactivate();
        context = std::move(context_);
        clipboard_manager = std::move(clipboard_manager_);
    }
    if (context) {
        context->Stop();
    }
    if (clipboard_manager) {
        clipboard_manager->Stop();
    }
}

void ClientClipboardModule::HandleMessage(
    const std::shared_ptr<Message>& message) {
    if (!message) {
        return;
    }
    std::lock_guard lock(lifecycle_mutex_);
    if (stopped_ || !clipboard_manager_) {
        return;
    }
    switch (message->type()) {
    case MessageType::kClipboardInfo:
        clipboard_manager_->OnRemoteClipboardMessage(message);
        break;
    case MessageType::kClipboardInfoResp:
        clipboard_manager_->OnRemoteClipboardRespMessage(message);
        break;
    case MessageType::kClipboardReqAtBegin:
    case MessageType::kClipboardReqBuffer:
    case MessageType::kClipboardReqAtEnd: {
        const auto bridge = runtime_bridge_;
        const auto context = context_;
        if (!context) {
            return;
        }
        context->PostWorkTask([bridge, message]() {
            switch (message->type()) {
            case MessageType::kClipboardReqAtBegin:
                bridge->OnRequestFileBegin(message);
                break;
            case MessageType::kClipboardReqBuffer:
                bridge->OnRequestFileBuffer(message);
                break;
            case MessageType::kClipboardReqAtEnd:
                bridge->OnRequestFileEnd(message);
                break;
            default:
                break;
            }
        });
        break;
    }
    case MessageType::kClipboardRespBuffer: {
        const auto manager = clipboard_manager_;
        const auto context = context_;
        if (context) {
            context->PostWorkTask([manager, message]() {
                manager->OnRemoteFileRespMessage(message);
            });
        }
        break;
    }
    default:
        break;
    }
}

void ClientClipboardModule::UpdateSettings(
    const ClientModuleSettings& settings) {
    std::lock_guard lock(lifecycle_mutex_);
    runtime_bridge_->UpdateSettings(settings);
}

bool ClientClipboardModule::IsClipboardEnabled() const {
    std::lock_guard lock(lifecycle_mutex_);
    return runtime_bridge_->IsEnabled();
}

}  // namespace px
