#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "px_common/expected.h"

namespace px_console {

enum class DeploymentKind : std::uint8_t {
    kOfficial,
    kPrivate,
};

enum class DeploymentIdentityError : std::uint8_t {
    kInvalid,
    kUntrusted,
    kSignature,
    kRejected,
};

struct TrustedVendorKey final {
    std::string keyId{};
    std::array<std::uint8_t, 32> publicKey{};
};

struct DeploymentTrustStore final {
    std::uint64_t trustEpoch{};
    std::vector<TrustedVendorKey> trustedKeys{};
};

struct DeploymentVerificationPolicy final {
    std::optional<std::string> expectedDeploymentId{};
    DeploymentKind expectedKind{DeploymentKind::kPrivate};
    std::uint64_t minimumCertificateVersion{};
    std::uint64_t minimumDescriptorRevision{};
    std::uint64_t minimumTrustEpoch{};
    std::uint64_t clientBuild{};
    std::uint16_t protocolVersion{};
};

struct VerifiedDeploymentIdentity final {
    std::string deploymentId{};
    DeploymentKind deploymentKind{DeploymentKind::kPrivate};
    std::array<std::uint8_t, 32> deploymentPublicKey{};
    std::uint64_t certificateVersion{};
    std::uint64_t descriptorRevision{};
    std::uint64_t trustEpoch{};
    std::int64_t descriptorExpiresAt{};
};

[[nodiscard]] px::Result<DeploymentTrustStore, DeploymentIdentityError> ParseDeploymentTrustStore(std::string_view canonicalJson);

[[nodiscard]] px::Result<VerifiedDeploymentIdentity, DeploymentIdentityError> VerifyDeploymentIdentity(std::string_view identityJson,
                                                                                                       const DeploymentTrustStore& trustStore,
                                                                                                       const DeploymentVerificationPolicy& policy,
                                                                                                       std::int64_t now);

[[nodiscard]] px::Result<bool, DeploymentIdentityError> VerifyDeploymentChallenge(const VerifiedDeploymentIdentity& identity,
                                                                                  std::string_view proofWire, std::string_view expectedNonce,
                                                                                  std::int64_t now);

[[nodiscard]] px::Result<std::string, DeploymentIdentityError> GenerateDeploymentChallengeNonce();

[[nodiscard]] px::Result<VerifiedDeploymentIdentity, DeploymentIdentityError> VerifyConsoleDeployment(const std::string& host, int port,
                                                                                                      const DeploymentTrustStore& trustStore,
                                                                                                      const DeploymentVerificationPolicy& policy);

}  // namespace px_console
