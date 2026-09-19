#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "panel_credential_vault.h"
#include "px_common/expected.h"
#include "px_console_client/deployment_identity.h"

namespace px::panel::product {

enum class PanelDistribution : std::uint8_t {
    Official,
    Customer,
};

enum class DeploymentGateError : std::uint8_t {
    InvalidInstallation,
    EndpointRejected,
    IdentityRejected,
    WatermarkRejected,
    WatermarkUnavailable,
};

class PanelDeploymentIdentityGate final {
public:
    static std::shared_ptr<PanelDeploymentIdentityGate> Create(const std::filesystem::path& executableDirectory, std::uint64_t clientBuild);

    PanelDeploymentIdentityGate(std::optional<px_console::DeploymentVerificationPolicy> policy,
                                std::optional<px_console::DeploymentTrustStore> trustStore, PanelDistribution distribution,
                                std::string officialConsoleAddress, std::shared_ptr<PanelCredentialVault> credentialVault);

    [[nodiscard]] bool IsReady() const;
    [[nodiscard]] PanelDistribution Distribution() const;
    [[nodiscard]] std::string OfficialConsoleAddress() const;

    [[nodiscard]] px::Result<px_console::VerifiedDeploymentIdentity, DeploymentGateError> VerifySelected(const std::string& consoleAddress,
                                                                                                         const std::string& host, int port);
    [[nodiscard]] px::Result<px_console::VerifiedDeploymentIdentity, DeploymentGateError> VerifyAndSelect(const std::string& consoleAddress,
                                                                                                          const std::string& host, int port);
    void InvalidateCache();

private:
    struct Watermark final {
        std::string deploymentId{};
        px_console::DeploymentKind deploymentKind{px_console::DeploymentKind::kPrivate};
        std::uint64_t certificateVersion{};
        std::uint64_t descriptorRevision{};
        std::uint64_t trustEpoch{};
    };

    struct Cache final {
        std::string consoleAddress{};
        px_console::VerifiedDeploymentIdentity identity{};
        std::int64_t validUntil{};
    };

    [[nodiscard]] px::Result<px_console::VerifiedDeploymentIdentity, DeploymentGateError> Verify(const std::string& consoleAddress,
                                                                                                 const std::string& host, int port,
                                                                                                 bool allowIdentitySwitch);
    [[nodiscard]] px::Result<std::optional<Watermark>, DeploymentGateError> ReadWatermark() const;
    [[nodiscard]] bool WriteWatermark(const Watermark& watermark) const;

    std::optional<px_console::DeploymentVerificationPolicy> policy_{};
    std::optional<px_console::DeploymentTrustStore> trustStore_{};
    PanelDistribution distribution_{PanelDistribution::Customer};
    std::string officialConsoleAddress_{};
    std::shared_ptr<PanelCredentialVault> credentialVault_{};
    mutable std::mutex mutex_{};
    std::optional<Cache> cache_{};
};

}  // namespace px::panel::product
