#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "px_client/plugin_interface/ct_plugin_interface.h"

namespace px {

class ClipboardFileWrapper;
class Message;

class ClipboardRuntimeBridge final {
public:
    ClipboardRuntimeBridge(
        const ClientPluginSettings& settings,
        ClientPluginEventCallback event_dispatcher);

    void UpdateSettings(const ClientPluginSettings& settings);
    void Deactivate();
    [[nodiscard]] bool IsEnabled() const;
    [[nodiscard]] ClientPluginSettings SettingsSnapshot() const;
    [[nodiscard]] const std::shared_ptr<std::atomic_bool>& LifetimeToken() const;

    void Dispatch(const std::shared_ptr<ClientPluginBaseEvent>& event) const;
    [[nodiscard]] bool RequestBuffer(
        const ClipboardFileWrapper& file_wrapper,
        int64_t request_index,
        int64_t request_start,
        unsigned long request_size) const;

    void OnRequestFileBegin(const std::shared_ptr<Message>& message) const;
    void OnRequestFileBuffer(const std::shared_ptr<Message>& message) const;
    void OnRequestFileEnd(const std::shared_ptr<Message>& message) const;

private:
    mutable std::mutex mutex_;
    ClientPluginSettings settings_;
    ClientPluginEventCallback event_dispatcher_;
    std::shared_ptr<std::atomic_bool> lifetime_token_ =
        std::make_shared<std::atomic_bool>(true);
};

}  // namespace px
