#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>

#include "client_launch_config.h"

namespace px::desktop {
class DesktopShell;
}

namespace px::client::imgui {

class ClientSession;
struct ClientTransferJob;

class ClientFileTransferAcceptance final {
public:
    ClientFileTransferAcceptance(std::reference_wrapper<px::desktop::DesktopShell> shell, std::shared_ptr<ClientSession> session,
                                 ClientFileTransferAcceptanceConfig config);

    void Tick();
    [[nodiscard]] bool Finished() const noexcept;
    [[nodiscard]] int ExitCode() const noexcept;

private:
    enum class Stage : std::uint8_t { WaitForConnection, AwaitUploadCancellation, Upload, Download, Cleanup, CompletionDelay, Finished };

    [[nodiscard]] std::optional<ClientTransferJob> FindJob(std::int32_t jobId) const;
    [[nodiscard]] std::optional<std::int32_t> FindReplacementUploadJob(std::int32_t previousJobId) const;
    void Fail(std::string reason);
    void Complete();

    std::reference_wrapper<px::desktop::DesktopShell> shell_;
    std::shared_ptr<ClientSession> session_{};
    ClientFileTransferAcceptanceConfig config_{};
    std::string remotePath_{};
    std::int32_t uploadJobId_{};
    std::int32_t downloadJobId_{};
    std::optional<std::chrono::steady_clock::time_point> uploadStartNotBefore_{};
    std::optional<std::chrono::steady_clock::time_point> completionNotBefore_{};
    std::chrono::steady_clock::time_point deadline_{std::chrono::steady_clock::now() + std::chrono::seconds{90}};
    Stage stage_{Stage::WaitForConnection};
    int exitCode_{1};
};

}  // namespace px::client::imgui
