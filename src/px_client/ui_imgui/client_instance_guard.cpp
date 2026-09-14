#include "client_instance_guard.h"

#include "px_common/win32/unique_win_handle.h"

#include <Windows.h>

#include <cstdint>
#include <format>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace px::client::imgui {
namespace {

std::uint64_t InstanceHash(const std::string_view remoteDeviceId) noexcept {
    constexpr std::uint64_t offset{14'695'981'039'346'656'037ULL};
    constexpr std::uint64_t prime{1'099'511'628'211ULL};
    std::uint64_t result{offset};
    for (const unsigned char value : remoteDeviceId) {
        if (value == ' ' || value == '\t' || value == '\r' || value == '\n')
            continue;
        const unsigned char normalized{value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value - 'A' + 'a') : value};
        result ^= normalized;
        result *= prime;
    }
    return result;
}

std::wstring ObjectName(const std::string_view remoteDeviceId, const ClientInstanceMode mode, const std::wstring_view suffix) {
    const std::wstring_view modeName{mode == ClientInstanceMode::FileTransfer ? L"FileTransfer" : L"Desktop"};
    return std::format(L"Local\\Pixels.Client.{}.{:016X}.{}", modeName, InstanceHash(remoteDeviceId), suffix);
}

} // namespace

struct ActivationState final {
    px::UniqueWinHandle event{};
};

struct ClientInstanceGuard::Impl final {
    px::UniqueWinHandle lease{};
    std::shared_ptr<ActivationState> activation{};
    std::jthread monitor{};
};

ClientInstanceGuard::ClientInstanceGuard(std::unique_ptr<Impl> impl) noexcept : impl_{std::move(impl)} {}
ClientInstanceGuard::ClientInstanceGuard(ClientInstanceGuard&&) noexcept = default;
ClientInstanceGuard& ClientInstanceGuard::operator=(ClientInstanceGuard&&) noexcept = default;
ClientInstanceGuard::~ClientInstanceGuard() = default;

ClientInstanceAcquireResult ClientInstanceGuard::Acquire(const std::string_view remoteDeviceId, const ClientInstanceMode mode) {
    if (remoteDeviceId.empty())
        return {.systemError = ERROR_INVALID_PARAMETER};

    px::UniqueWinHandle activation{CreateEventW(nullptr, FALSE, FALSE, ObjectName(remoteDeviceId, mode, L"Activate").c_str())};
    if (!activation)
        return {.systemError = GetLastError()};

    SetLastError(ERROR_SUCCESS);
    px::UniqueWinHandle lease{CreateMutexW(nullptr, FALSE, ObjectName(remoteDeviceId, mode, L"Lease").c_str())};
    const DWORD createStatus{GetLastError()};
    if (!lease)
        return {.systemError = createStatus};
    if (createStatus == ERROR_ALREADY_EXISTS) {
        static_cast<void>(AllowSetForegroundWindow(ASFW_ANY));
        if (SetEvent(activation.get()) == FALSE)
            return {.systemError = GetLastError()};
        return {.activatedExisting = true};
    }

    auto impl = std::make_unique<Impl>();
    impl->lease = std::move(lease);
    impl->activation = std::make_shared<ActivationState>(ActivationState{.event = std::move(activation)});
    return {.instance = ClientInstanceGuard{std::move(impl)}};
}

bool ClientInstanceGuard::ConsumeActivationRequest() const noexcept {
    return impl_ && WaitForSingleObject(impl_->activation->event.get(), 0U) == WAIT_OBJECT_0;
}

bool ClientInstanceGuard::StartActivationMonitor(std::function<void()> callback) noexcept {
    if (!impl_ || !callback || impl_->monitor.joinable())
        return false;
    const auto activation = impl_->activation;
    try {
        impl_->monitor = std::jthread{[activation, callback = std::move(callback)](const std::stop_token stopToken) {
            while (!stopToken.stop_requested()) {
                const DWORD waitResult{WaitForSingleObject(activation->event.get(), 100U)};
                if (waitResult == WAIT_OBJECT_0)
                    callback();
                else if (waitResult == WAIT_FAILED)
                    return;
            }
        }};
        return true;
    } catch (const std::system_error&) {
        return false;
    }
}

} // namespace px::client::imgui
