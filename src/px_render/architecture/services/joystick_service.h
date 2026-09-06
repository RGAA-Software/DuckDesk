#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "modules/builtin_module_catalog.h"

namespace px {
class Data;
class Message;
}

namespace px::render {

inline constexpr std::string_view kJoystickModuleId =
    "102a229e-295d-444e-9ca0-b6644f3198f6";

class JoystickBackend {
public:
    using RumbleCallback = std::function<void(
        const std::string&, std::uint8_t, std::uint8_t)>;

    virtual ~JoystickBackend() = default;
    virtual void SetRumbleCallback(RumbleCallback callback) = 0;
    [[nodiscard]] virtual bool PrepareConnection() = 0;
    [[nodiscard]] virtual bool AllocateController(
        const std::string& stream_id) = 0;
    virtual void ReplayJoystickEvent(
        const std::string& stream_id,
        const std::shared_ptr<Message>& message) = 0;
    virtual void RemoveController(const std::string& stream_id) = 0;
    virtual void Shutdown() = 0;
};

struct JoystickServiceSnapshot final {
    bool running{false};
    bool enabled{true};
    bool backend_ready{false};
    std::uint64_t allocated_controllers{0};
    std::uint64_t replayed_events{0};
    std::uint64_t rejected_messages{0};
    std::uint64_t rumble_events{0};
    std::uint64_t rumble_send_failures{0};
};

class JoystickService final
    : public std::enable_shared_from_this<JoystickService> {
public:
    using BackendFactory = std::function<std::shared_ptr<JoystickBackend>()>;
    using SendCallback = std::function<bool(
        const std::string&, const std::string&, const std::shared_ptr<Data>&)>;

    [[nodiscard]] static std::shared_ptr<JoystickService> Create(
        BackendFactory backend_factory = {}, SendCallback send_callback = {});
    JoystickService(
        BackendFactory backend_factory, SendCallback send_callback);
    ~JoystickService();

    JoystickService(const JoystickService&) = delete;
    JoystickService& operator=(const JoystickService&) = delete;

    [[nodiscard]] BuiltinModuleRegistration MakeRegistration();
    [[nodiscard]] ModuleLifecycleResult Start();
    [[nodiscard]] ModuleLifecycleResult Stop();
    [[nodiscard]] ModuleLifecycleResult SetEnabled(bool enabled);

    void HandleMessage(
        const std::shared_ptr<Message>& message,
        const std::string& transport_id = {});
    void HandleClientDisconnected(const std::string& stream_id);
    [[nodiscard]] JoystickServiceSnapshot Snapshot() const;

private:
    [[nodiscard]] std::shared_ptr<JoystickBackend> EnsureBackend();
    void HandleRumble(
        const std::string& stream_id,
        std::uint8_t strong_motor,
        std::uint8_t weak_motor);

    mutable std::mutex mutex_;
    mutable std::mutex backend_operation_mutex_;
    BackendFactory backend_factory_;
    SendCallback send_callback_;
    std::shared_ptr<JoystickBackend> backend_;
    std::unordered_map<std::string, std::string> routes_;
    bool running_{false};
    bool enabled_{true};
    bool backend_ready_{false};
    std::uint64_t allocated_controllers_{0};
    std::uint64_t replayed_events_{0};
    std::uint64_t rejected_messages_{0};
    std::uint64_t rumble_events_{0};
    std::uint64_t rumble_send_failures_{0};
};

}  // namespace px::render
