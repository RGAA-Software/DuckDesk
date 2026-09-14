#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace px::client::imgui {

enum class ClientInstanceMode : std::uint8_t {
    Desktop,
    FileTransfer,
};

struct ClientInstanceAcquireResult;

class ClientInstanceGuard final {
  public:
    static ClientInstanceAcquireResult Acquire(std::string_view remoteDeviceId, ClientInstanceMode mode);

    ClientInstanceGuard(ClientInstanceGuard&&) noexcept;
    ClientInstanceGuard& operator=(ClientInstanceGuard&&) noexcept;
    ~ClientInstanceGuard();

    ClientInstanceGuard(const ClientInstanceGuard&) = delete;
    ClientInstanceGuard& operator=(const ClientInstanceGuard&) = delete;

    [[nodiscard]] bool ConsumeActivationRequest() const noexcept;
    [[nodiscard]] bool StartActivationMonitor(std::function<void()> callback) noexcept;

  private:
    struct Impl;

    explicit ClientInstanceGuard(std::unique_ptr<Impl> impl) noexcept;

    std::unique_ptr<Impl> impl_{};
};

struct ClientInstanceAcquireResult final {
    std::optional<ClientInstanceGuard> instance{};
    bool activatedExisting{};
    std::uint32_t systemError{};
};

} // namespace px::client::imgui
