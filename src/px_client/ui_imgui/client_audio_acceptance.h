#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>

namespace px::desktop {
class DesktopShell;
}

namespace px::client::imgui {

class ClientSession;

class ClientAudioAcceptance final {
public:
    ClientAudioAcceptance(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session);

    void Tick();
    [[nodiscard]] int ExitCode() const noexcept;

private:
    void Finish(int exitCode, std::string_view result);

    std::reference_wrapper<px::desktop::DesktopShell> shell_;
    std::shared_ptr<ClientSession> session_{};
    std::chrono::steady_clock::time_point deadline_{std::chrono::steady_clock::now() + std::chrono::seconds{60}};
    std::optional<std::chrono::steady_clock::time_point> successObservedAt_{};
    bool finished_{};
    int exitCode_{1};
};

}  // namespace px::client::imgui
