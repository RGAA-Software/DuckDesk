#pragma once

#include <memory>
#include <mutex>

#include "px_client/modules/client_module_settings.h"

namespace px {

class ClipboardManager;
class ClipboardRuntimeBridge;
class ClientModuleContext;
class ClientModuleServices;
class Message;

class ClientClipboardModule final {
public:
    explicit ClientClipboardModule(
        std::weak_ptr<ClientModuleServices> services);
    ~ClientClipboardModule();

    ClientClipboardModule(const ClientClipboardModule&) = delete;
    ClientClipboardModule& operator=(const ClientClipboardModule&) = delete;

    bool Start(const ClientModuleConfig& config);
    void Stop();
    void HandleMessage(const std::shared_ptr<Message>& message);
    void UpdateSettings(const ClientModuleSettings& settings);
    [[nodiscard]] bool IsClipboardEnabled() const;

private:
    std::shared_ptr<ClientModuleContext> context_;
    std::shared_ptr<ClipboardManager> clipboard_manager_;
    std::shared_ptr<ClipboardRuntimeBridge> runtime_bridge_;
    mutable std::mutex lifecycle_mutex_;
    bool stopped_ = true;
};

}  // namespace px
