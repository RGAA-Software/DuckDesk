#include "panel_deployment_identity_gate.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iterator>
#include <limits>
#include <nlohmann/json.hpp>
#include <string_view>
#include <utility>

#include "panel_config_store.h"

namespace px::panel::product {
namespace {

using Json = nlohmann::json;

constexpr std::size_t kConfigurationLimit{64 * 1024};
constexpr std::string_view kWatermarkCredential{"watermark"};
#ifndef PX_PRODUCT_DISTRIBUTION
#define PX_PRODUCT_DISTRIBUTION "development"
#endif
constexpr std::string_view kProductDistribution{PX_PRODUCT_DISTRIBUTION};

struct ParsedPolicy final {
    px_console::DeploymentVerificationPolicy verification{};
    PanelDistribution distribution{PanelDistribution::Customer};
    std::string officialConsoleAddress{};
};

std::optional<std::string> ReadBoundedFile(const std::filesystem::path& path) {
    std::error_code error{};
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0 || size > kConfigurationLimit) return std::nullopt;
    std::ifstream input{path, std::ios::binary};
    if (!input) return std::nullopt;
    std::string bytes{};
    bytes.reserve(static_cast<std::size_t>(size));
    bytes.assign(std::istreambuf_iterator<char>{input}, std::istreambuf_iterator<char>{});
    return bytes.size() == size ? std::optional{std::move(bytes)} : std::nullopt;
}

template <std::size_t FieldCount>
bool HasExactFields(const Json& value, const std::array<std::string_view, FieldCount>& fields) {
    if (!value.is_object() || value.size() != fields.size()) return false;
    return std::ranges::all_of(fields, [&value](const auto field) { return value.contains(field); });
}

std::optional<std::uint64_t> PositiveNumber(const Json& value) {
    if (!value.is_number_unsigned()) return std::nullopt;
    const auto number = value.get<std::uint64_t>();
    return number > 0 ? std::optional{number} : std::nullopt;
}

bool IsCanonicalUuid(const std::string_view value) {
    if (value.size() != 36 || value == "00000000-0000-0000-0000-000000000000") return false;
    for (std::size_t index{}; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') return false;
        } else if (!std::isdigit(static_cast<unsigned char>(value[index])) && (value[index] < 'a' || value[index] > 'f')) {
            return false;
        }
    }
    return true;
}

bool IsCanonicalOemId(const std::string_view oemId) {
    return oemId.size() >= 3 && oemId.size() <= 32 && oemId.front() != '-' && oemId.back() != '-' && !oemId.contains("--") && oemId != "pixels" &&
           oemId != "official" && oemId != "customer" && oemId != "oem" && std::ranges::all_of(oemId, [](const unsigned char character) {
               return std::islower(character) != 0 || std::isdigit(character) != 0 || character == '-';
           });
}

std::optional<ParsedPolicy> ParsePolicy(const std::string_view bytes, const std::uint64_t clientBuild) {
    constexpr std::array<std::string_view, 10> fields{
        "schema_version",
        "distribution",
        "release_namespace",
        "oem_id",
        "expected_deployment_id",
        "official_console_origin",
        "minimum_certificate_version",
        "minimum_descriptor_revision",
        "minimum_trust_epoch",
        "protocol_version",
    };
    try {
        const auto value = Json::parse(bytes);
        if (!HasExactFields(value, fields) || value["schema_version"] != 2 || !value["distribution"].is_string() ||
            !value["release_namespace"].is_string() || (!value["oem_id"].is_null() && !value["oem_id"].is_string())) {
            return std::nullopt;
        }
        const auto minimumCertificateVersion = PositiveNumber(value["minimum_certificate_version"]);
        const auto minimumDescriptorRevision = PositiveNumber(value["minimum_descriptor_revision"]);
        const auto minimumTrustEpoch = PositiveNumber(value["minimum_trust_epoch"]);
        const auto protocolVersion = PositiveNumber(value["protocol_version"]);
        if (!minimumCertificateVersion || !minimumDescriptorRevision || !minimumTrustEpoch || !protocolVersion ||
            *protocolVersion > std::numeric_limits<std::uint16_t>::max() || clientBuild == 0) {
            return std::nullopt;
        }
        const auto distributionText = value["distribution"].get<std::string>();
        const auto releaseNamespace = value["release_namespace"].get<std::string>();
        const auto oemId = value["oem_id"].is_string() ? std::optional{value["oem_id"].get<std::string>()} : std::nullopt;
        ParsedPolicy policy{};
        policy.verification.expectedReleaseNamespace = releaseNamespace;
        policy.verification.expectedOemId = oemId;
        policy.verification.minimumCertificateVersion = *minimumCertificateVersion;
        policy.verification.minimumDescriptorRevision = *minimumDescriptorRevision;
        policy.verification.minimumTrustEpoch = *minimumTrustEpoch;
        policy.verification.clientBuild = clientBuild;
        policy.verification.protocolVersion = static_cast<std::uint16_t>(*protocolVersion);
        if (distributionText == "official") {
            if (releaseNamespace != "pixels.official" || oemId || !value["expected_deployment_id"].is_string() ||
                !value["official_console_origin"].is_string()) {
                return std::nullopt;
            }
            const auto deploymentId = value["expected_deployment_id"].get<std::string>();
            const auto origin = value["official_console_origin"].get<std::string>();
            const auto endpoint = ParseConsoleHttpsOrigin(origin);
            if (!IsCanonicalUuid(deploymentId) || !endpoint || endpoint->baseUrl != origin) {
                return std::nullopt;
            }
            policy.distribution = PanelDistribution::Official;
            policy.verification.expectedDeploymentId = deploymentId;
            policy.verification.expectedKind = px_console::DeploymentKind::kOfficial;
            policy.verification.expectedDistribution = px_console::DeploymentDistribution::kOfficial;
            policy.officialConsoleAddress = origin;
        } else if (distributionText == "customer") {
            if (releaseNamespace != "pixels.customer" || oemId || !value["expected_deployment_id"].is_null() ||
                !value["official_console_origin"].is_null()) {
                return std::nullopt;
            }
            policy.distribution = PanelDistribution::Customer;
            policy.verification.expectedKind = px_console::DeploymentKind::kPrivate;
            policy.verification.expectedDistribution = px_console::DeploymentDistribution::kCustomer;
        } else if (distributionText == "oem") {
            if (!value["expected_deployment_id"].is_null() || !value["official_console_origin"].is_null() || !oemId || !IsCanonicalOemId(*oemId) ||
                releaseNamespace != "oem." + *oemId) {
                return std::nullopt;
            }
            policy.distribution = PanelDistribution::Oem;
            policy.verification.expectedKind = px_console::DeploymentKind::kPrivate;
            policy.verification.expectedDistribution = px_console::DeploymentDistribution::kOem;
        } else {
            return std::nullopt;
        }
        return policy;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::int64_t UnixTimeNow() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

}  // namespace

std::shared_ptr<PanelDeploymentIdentityGate> PanelDeploymentIdentityGate::Create(const std::filesystem::path& executableDirectory,
                                                                                 const std::uint64_t clientBuild) {
    const auto policyBytes = ReadBoundedFile(executableDirectory / "resources" / "deployment" / "deployment-policy.json");
    const auto trustStoreBytes = ReadBoundedFile(executableDirectory / "resources" / "deployment" / "deployment-trust.json");
    const auto parsedPolicy = policyBytes ? ParsePolicy(*policyBytes, clientBuild) : std::nullopt;
    const auto parsedTrustStore = trustStoreBytes ? px_console::ParseDeploymentTrustStore(*trustStoreBytes)
                                                  : px::Result<px_console::DeploymentTrustStore, px_console::DeploymentIdentityError>{
                                                        std::unexpected{px_console::DeploymentIdentityError::kInvalid}};
    const auto compiledDistribution = kProductDistribution == "official"   ? PanelDistribution::Official
                                      : kProductDistribution == "customer" ? PanelDistribution::Customer
                                                                           : PanelDistribution::Oem;
    const bool policyMatchesExecutable =
        parsedPolicy && ((kProductDistribution == "official" && parsedPolicy->distribution == PanelDistribution::Official) ||
                         (kProductDistribution == "customer" && parsedPolicy->distribution == PanelDistribution::Customer) ||
                         (kProductDistribution == "oem" && parsedPolicy->distribution == PanelDistribution::Oem));
    const auto acceptedPolicy = policyMatchesExecutable ? parsedPolicy : std::nullopt;
    const auto trustStore = parsedTrustStore && acceptedPolicy && parsedTrustStore->trustEpoch == acceptedPolicy->verification.minimumTrustEpoch
                                ? std::optional{*parsedTrustStore}
                                : std::nullopt;
    return std::make_shared<PanelDeploymentIdentityGate>(
        acceptedPolicy ? std::optional{acceptedPolicy->verification} : std::nullopt, trustStore, compiledDistribution,
        acceptedPolicy ? acceptedPolicy->officialConsoleAddress : std::string{}, PanelCredentialVault::Create("DeploymentIdentity"));
}

PanelDeploymentIdentityGate::PanelDeploymentIdentityGate(std::optional<px_console::DeploymentVerificationPolicy> policy,
                                                         std::optional<px_console::DeploymentTrustStore> trustStore,
                                                         const PanelDistribution distribution, std::string officialConsoleAddress,
                                                         std::shared_ptr<PanelCredentialVault> credentialVault)
    : policy_{std::move(policy)},
      trustStore_{std::move(trustStore)},
      distribution_{distribution},
      officialConsoleAddress_{std::move(officialConsoleAddress)},
      credentialVault_{std::move(credentialVault)} {}

bool PanelDeploymentIdentityGate::IsReady() const {
    return policy_.has_value() && trustStore_.has_value() && credentialVault_ && trustStore_->trustEpoch == policy_->minimumTrustEpoch;
}

PanelDistribution PanelDeploymentIdentityGate::Distribution() const { return distribution_; }

std::string PanelDeploymentIdentityGate::OfficialConsoleAddress() const { return officialConsoleAddress_; }

px::Result<px_console::VerifiedDeploymentIdentity, DeploymentGateError> PanelDeploymentIdentityGate::VerifySelected(const std::string& consoleAddress,
                                                                                                                    const std::string& host,
                                                                                                                    const int port) {
    return Verify(consoleAddress, host, port, false);
}

px::Result<px_console::VerifiedDeploymentIdentity, DeploymentGateError> PanelDeploymentIdentityGate::VerifyAndSelect(
    const std::string& consoleAddress, const std::string& host, const int port) {
    return Verify(consoleAddress, host, port, distribution_ != PanelDistribution::Official);
}

void PanelDeploymentIdentityGate::InvalidateCache() {
    const std::scoped_lock lock{mutex_};
    cache_.reset();
}

px::Result<px_console::VerifiedDeploymentIdentity, DeploymentGateError> PanelDeploymentIdentityGate::Verify(const std::string& consoleAddress,
                                                                                                            const std::string& host, const int port,
                                                                                                            const bool allowIdentitySwitch) {
    if (!IsReady()) return std::unexpected{DeploymentGateError::InvalidInstallation};
    if (distribution_ == PanelDistribution::Official && consoleAddress != officialConsoleAddress_) {
        return std::unexpected{DeploymentGateError::EndpointRejected};
    }
    const auto now = UnixTimeNow();
    if (!allowIdentitySwitch) {
        const std::scoped_lock lock{mutex_};
        if (cache_ && cache_->consoleAddress == consoleAddress && cache_->validUntil > now) return cache_->identity;
    }

    const auto current = ReadWatermark();
    if (!current) return std::unexpected{current.error()};
    auto verificationPolicy = *policy_;
    if (distribution_ != PanelDistribution::Official && *current && !allowIdentitySwitch) {
        verificationPolicy.expectedDeploymentId = (*current)->deploymentId;
    }
    const auto verified = px_console::VerifyConsoleDeployment(host, port, *trustStore_, verificationPolicy);
    if (!verified) return std::unexpected{DeploymentGateError::IdentityRejected};
    const Watermark candidate{.deploymentId = verified->deploymentId,
                              .deploymentKind = verified->deploymentKind,
                              .distribution = verified->distribution,
                              .releaseNamespace = verified->releaseNamespace,
                              .oemId = verified->oemId,
                              .certificateVersion = verified->certificateVersion,
                              .descriptorRevision = verified->descriptorRevision,
                              .trustEpoch = verified->trustEpoch};
    if (*current) {
        const auto& stored = **current;
        const bool sameIdentity = stored.deploymentId == candidate.deploymentId && stored.deploymentKind == candidate.deploymentKind &&
                                  stored.distribution == candidate.distribution && stored.releaseNamespace == candidate.releaseNamespace &&
                                  stored.oemId == candidate.oemId;
        const bool monotonic = sameIdentity && candidate.certificateVersion >= stored.certificateVersion &&
                               candidate.descriptorRevision >= stored.descriptorRevision && candidate.trustEpoch >= stored.trustEpoch;
        if (!monotonic && !(allowIdentitySwitch && stored.deploymentId != candidate.deploymentId)) {
            return std::unexpected{DeploymentGateError::WatermarkRejected};
        }
    }
    if (!WriteWatermark(candidate)) return std::unexpected{DeploymentGateError::WatermarkUnavailable};
    {
        const std::scoped_lock lock{mutex_};
        cache_ = Cache{.consoleAddress = consoleAddress, .identity = *verified, .validUntil = std::min(now + 15, verified->descriptorExpiresAt)};
    }
    return *verified;
}

px::Result<std::optional<PanelDeploymentIdentityGate::Watermark>, DeploymentGateError> PanelDeploymentIdentityGate::ReadWatermark() const {
    const auto stored = credentialVault_->Read(std::string{kWatermarkCredential});
    if (!stored) return std::optional<Watermark>{};
    constexpr std::array<std::string_view, 9> fields{"schema_version",      "deployment_id",       "deployment_kind",
                                                     "distribution",        "release_namespace",   "oem_id",
                                                     "certificate_version", "descriptor_revision", "trust_epoch"};
    try {
        const auto value = Json::parse(*stored);
        if (!HasExactFields(value, fields) || value["schema_version"] != 2 || !value["deployment_id"].is_string() ||
            !value["deployment_kind"].is_string() || !value["distribution"].is_string() || !value["release_namespace"].is_string() ||
            (!value["oem_id"].is_null() && !value["oem_id"].is_string())) {
            return std::unexpected{DeploymentGateError::WatermarkRejected};
        }
        const auto deploymentId = value["deployment_id"].get<std::string>();
        const auto kindText = value["deployment_kind"].get<std::string>();
        const auto distributionText = value["distribution"].get<std::string>();
        const auto releaseNamespace = value["release_namespace"].get<std::string>();
        const auto oemId = value["oem_id"].is_string() ? std::optional{value["oem_id"].get<std::string>()} : std::nullopt;
        const auto certificateVersion = PositiveNumber(value["certificate_version"]);
        const auto descriptorRevision = PositiveNumber(value["descriptor_revision"]);
        const auto trustEpoch = PositiveNumber(value["trust_epoch"]);
        if (!IsCanonicalUuid(deploymentId) || (kindText != "official" && kindText != "private") ||
            (distributionText != "official" && distributionText != "customer" && distributionText != "oem") || !certificateVersion ||
            !descriptorRevision || !trustEpoch) {
            return std::unexpected{DeploymentGateError::WatermarkRejected};
        }
        return std::optional{
            Watermark{.deploymentId = deploymentId,
                      .deploymentKind = kindText == "official" ? px_console::DeploymentKind::kOfficial : px_console::DeploymentKind::kPrivate,
                      .distribution = distributionText == "official"   ? px_console::DeploymentDistribution::kOfficial
                                      : distributionText == "customer" ? px_console::DeploymentDistribution::kCustomer
                                                                       : px_console::DeploymentDistribution::kOem,
                      .releaseNamespace = releaseNamespace,
                      .oemId = oemId,
                      .certificateVersion = *certificateVersion,
                      .descriptorRevision = *descriptorRevision,
                      .trustEpoch = *trustEpoch}};
    } catch (const std::exception&) {
        return std::unexpected{DeploymentGateError::WatermarkRejected};
    }
}

bool PanelDeploymentIdentityGate::WriteWatermark(const Watermark& watermark) const {
    const auto distribution = watermark.distribution == px_console::DeploymentDistribution::kOfficial   ? "official"
                              : watermark.distribution == px_console::DeploymentDistribution::kCustomer ? "customer"
                                                                                                        : "oem";
    const Json value{{"schema_version", 2},
                     {"deployment_id", watermark.deploymentId},
                     {"deployment_kind", watermark.deploymentKind == px_console::DeploymentKind::kOfficial ? "official" : "private"},
                     {"distribution", distribution},
                     {"release_namespace", watermark.releaseNamespace},
                     {"oem_id", watermark.oemId ? Json(*watermark.oemId) : Json(nullptr)},
                     {"certificate_version", watermark.certificateVersion},
                     {"descriptor_revision", watermark.descriptorRevision},
                     {"trust_epoch", watermark.trustEpoch}};
    return credentialVault_->Write(std::string{kWatermarkCredential}, value.dump());
}

}  // namespace px::panel::product
