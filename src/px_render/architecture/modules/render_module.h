#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <d3d11.h>
#include <wrl/client.h>

#include "px_render/architecture/config/render_runtime_settings.h"
#include "px_render/architecture/events/render_event.h"

namespace px {

class AppBaseEvent;
class CaptureMonitorInfoMessage;
class Message;
class PxAsyncRuntime;
class RenderExecutionContext;

enum class RenderModuleKind {
    kSource,
    kEncoder,
    kNetwork,
};

enum class RenderModuleLifecycle {
    kCreated,
    kRunning,
    kStopping,
    kDestroyed,
};

// Typed startup configuration for executable-owned modules. It carries only
// owned values and never transports type-erased ownership.
struct RenderModuleConfiguration final {
    std::shared_ptr<PxAsyncRuntime> async_runtime;
    std::string instance_name;
    std::string base_path;
    std::wstring base_data_path;
    std::string capture_audio_device_id;
    std::int64_t ws_listen_port{0};
    std::int64_t udp_listen_port{0};
    std::string device_id;
    bool direct_allow_takeover{true};
    std::string relay_device_id;
    bool relay_enabled{true};
    std::string relay_host;
    std::string relay_port;
    int language{1};
    std::string appkey;
    std::string app_mode;
    int udp_fec_percent{20};
    int udp_mtu{1400};
};

using RenderModuleSettings = RenderRuntimeSettings;

class ModuleEventChannel;

// Lifetime:
// - Executable-owned modules are held by RenderModuleRegistry through shared_ptr.
// - Queued callbacks retain only ModuleEventChannel, never the module instance.
// - Stop disables delivery; Destroy drains the owned task context.
//
// Threading:
// - Lifecycle state is atomic.
// - Event callback replacement and delivery are serialized by the channel.
// - Derived asynchronous work must capture weak_ptr ownership.
class RenderModule : public std::enable_shared_from_this<RenderModule> {
  public:
    RenderModule();
    virtual ~RenderModule() = default;

    RenderModule(const RenderModule&) = delete;
    RenderModule& operator=(const RenderModule&) = delete;

    [[nodiscard]] std::shared_ptr<RenderExecutionContext> ExecutionContext() const;

    [[nodiscard]] virtual std::string Id() const = 0;
    [[nodiscard]] virtual std::string Name() const;
    [[nodiscard]] virtual std::string Author() const;
    [[nodiscard]] virtual std::string Description() const;
    [[nodiscard]] virtual std::string VersionName() const;
    [[nodiscard]] virtual std::uint32_t VersionCode() const;
    [[nodiscard]] virtual RenderModuleKind Kind() const = 0;

    [[nodiscard]] bool IsEnabled() const noexcept;
    virtual void SetEnabled(bool enabled) noexcept;
    [[nodiscard]] virtual bool IsWorking() const;

    virtual bool Start(const RenderModuleConfiguration& configuration);
    virtual bool Resume();
    virtual bool Stop();
    virtual bool Destroy();
    [[nodiscard]] bool IsStoppingOrDestroyed() const noexcept;

    void PostWorkTask(std::function<void()>&& task);
    void PostUiTask(std::function<void()>&& task);
    void PostDelayedUiTask(int milliseconds, std::function<void()>&& task);

    void SetEventCallback(const RenderEventCallback& callback);
    void EmitEvent(RenderEvent event);
    void EmitEventImmediately(RenderEvent event);
    [[nodiscard]] RenderEventCallback MakeImmediateEventDispatcher() const;

    virtual void Tick1Second();
    virtual void RequestKeyFrame();
    virtual void HandleCommand(const std::string& command);
    virtual void OnClientConnected(const std::string& visitor_device_id, const std::string& stream_id, const std::string& transport);
    virtual void OnClientDisconnected(const std::string& visitor_device_id, const std::string& stream_id);
    virtual void HandleMessage(const std::shared_ptr<Message>& message);
    virtual void UpdateSettings(const RenderModuleSettings& settings);
    virtual void HandleAppEvent(const std::shared_ptr<AppBaseEvent>& event);
    virtual void UpdateCaptureMonitorInfo(const CaptureMonitorInfoMessage& message);

    [[nodiscard]] RenderModuleSettings Settings() const;
    [[nodiscard]] bool HasNoConnectedClients() const noexcept;
    void ReportDataSent(std::size_t bytes);

    void UpdateD3DResources(std::uint64_t adapter_uid, const Microsoft::WRL::ComPtr<ID3D11Device>& device,
                            const Microsoft::WRL::ComPtr<ID3D11DeviceContext>& context);
    void ClearD3DResources(std::uint64_t adapter_uid);

  protected:
    std::shared_ptr<RenderExecutionContext> execution_context_;
    std::atomic_bool stopped_{false};
    std::atomic_bool destroyed_{false};
    std::atomic<RenderModuleLifecycle> lifecycle_{RenderModuleLifecycle::kCreated};
    RenderModuleConfiguration configuration_;
    RenderModuleSettings settings_;
    std::atomic_bool enabled_{true};
    std::atomic_int64_t no_connected_clients_counter_{0};

  public:
    std::map<std::uint64_t, Microsoft::WRL::ComPtr<ID3D11Device>> d3d11_devices_;
    std::map<std::uint64_t, Microsoft::WRL::ComPtr<ID3D11DeviceContext>> d3d11_device_contexts_;

  private:
    std::shared_ptr<ModuleEventChannel> event_channel_;
};

} // namespace px
