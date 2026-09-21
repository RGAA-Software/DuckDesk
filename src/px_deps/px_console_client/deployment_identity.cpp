#include "deployment_identity.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <utility>

#include "console_http_client.h"
#include "px_common/http_client.h"

namespace px_console {
namespace {

using OrderedJson = nlohmann::ordered_json;

constexpr std::string_view kCertificatePrefix{"PXDC2"};
constexpr char kCertificateDomainBytes[] = "Pixels-Deployment-Certificate-v2\0";
constexpr std::string_view kCertificateDomain{kCertificateDomainBytes, sizeof(kCertificateDomainBytes) - 1};
constexpr std::string_view kDescriptorPrefix{"PXDD2"};
constexpr char kDescriptorDomainBytes[] = "Pixels-Platform-Descriptor-v2\0";
constexpr std::string_view kDescriptorDomain{kDescriptorDomainBytes, sizeof(kDescriptorDomainBytes) - 1};
constexpr std::string_view kChallengePrefix{"PXDP1"};
constexpr char kChallengeDomainBytes[] = "Pixels-Deployment-Challenge-v1\0";
constexpr std::string_view kChallengeDomain{kChallengeDomainBytes, sizeof(kChallengeDomainBytes) - 1};
constexpr std::size_t kMaximumWireBytes{16 * 1024};

using PublicKeyHandle = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using DigestContextHandle = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

struct DecodedWire final {
    std::vector<std::uint8_t> payload{};
    std::array<std::uint8_t, 64> signature{};
};

struct Certificate final {
    std::string deploymentId{};
    DeploymentKind deploymentKind{DeploymentKind::kPrivate};
    DeploymentDistribution distribution{DeploymentDistribution::kCustomer};
    std::string releaseNamespace{};
    std::optional<std::string> oemId{};
    std::array<std::uint8_t, 32> deploymentPublicKey{};
    std::uint64_t certificateVersion{};
    std::int64_t notBefore{};
    std::int64_t expiresAt{};
    std::string issuerKeyId{};
};

struct Descriptor final {
    std::string deploymentId{};
    DeploymentKind deploymentKind{DeploymentKind::kPrivate};
    DeploymentDistribution distribution{DeploymentDistribution::kCustomer};
    std::string releaseNamespace{};
    std::optional<std::string> oemId{};
    std::uint64_t descriptorRevision{};
    std::uint64_t trustEpoch{};
    std::int64_t issuedAt{};
    std::int64_t expiresAt{};
    std::uint64_t minimumClientBuild{};
    std::uint16_t minimumProtocolVersion{};
    std::uint16_t maximumProtocolVersion{};
};

constexpr std::array<std::string_view, 11> kCertificateFields{
    "schema_version",      "deployment_id", "deployment_kind", "distribution",  "release_namespace", "oem_id", "deployment_public_key_hex",
    "certificate_version", "not_before",    "expires_at",      "issuer_key_id",
};
constexpr std::array<std::string_view, 16> kDescriptorFields{
    "schema_version",
    "deployment_id",
    "deployment_kind",
    "distribution",
    "release_namespace",
    "oem_id",
    "descriptor_revision",
    "trust_epoch",
    "issued_at",
    "expires_at",
    "minimum_client_build",
    "api_versions",
    "minimum_protocol_version",
    "maximum_protocol_version",
    "authentication_methods",
    "registration_policy",
};
constexpr std::array<std::string_view, 2> kDescriptorPathFields{"console_api_path", "node_control_path"};
constexpr std::array<std::string_view, 6> kChallengeFields{
    "schema_version", "deployment_id", "descriptor_revision", "nonce", "issued_at", "expires_at",
};

template <std::size_t FieldCount>
bool HasExactOrderedFields(const OrderedJson& value, const std::array<std::string_view, FieldCount>& fields) {
    if (!value.is_object() || value.size() != FieldCount) {
        return false;
    }
    std::size_t fieldIndex{};
    for (const auto& [fieldName, ignoredValue] : value.items()) {
        static_cast<void>(ignoredValue);
        if (fieldName != fields[fieldIndex++]) {
            return false;
        }
    }
    return true;
}

bool HasExactDescriptorFields(const OrderedJson& value) {
    if (!value.is_object() || value.size() != kDescriptorFields.size() + kDescriptorPathFields.size()) {
        return false;
    }
    std::size_t fieldIndex{};
    for (const auto& [fieldName, ignoredValue] : value.items()) {
        static_cast<void>(ignoredValue);
        const auto expected =
            fieldIndex < kDescriptorFields.size() ? kDescriptorFields[fieldIndex] : kDescriptorPathFields[fieldIndex - kDescriptorFields.size()];
        if (fieldName != expected) {
            return false;
        }
        ++fieldIndex;
    }
    return true;
}

bool IsLowerHex(const std::string_view value, const std::size_t length) {
    return value.size() == length && std::ranges::all_of(value, [](const unsigned char character) {
               return std::isdigit(character) != 0 || (character >= 'a' && character <= 'f');
           });
}

template <std::size_t ByteCount>
std::optional<std::array<std::uint8_t, ByteCount>> DecodeLowerHex(const std::string_view value) {
    if (!IsLowerHex(value, ByteCount * 2)) {
        return std::nullopt;
    }
    const auto hexValue = [](const unsigned char character) -> std::uint8_t {
        return character <= '9' ? static_cast<std::uint8_t>(character - '0') : static_cast<std::uint8_t>(character - 'a' + 10);
    };
    std::array<std::uint8_t, ByteCount> decoded{};
    for (std::size_t index{}; index < decoded.size(); ++index) {
        decoded[index] = static_cast<std::uint8_t>((hexValue(value[index * 2]) << 4) | hexValue(value[index * 2 + 1]));
    }
    return decoded;
}

std::string EncodeLowerHex(const std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string encoded(bytes.size() * 2, '\0');
    for (std::size_t index{}; index < bytes.size(); ++index) {
        encoded[index * 2] = digits[bytes[index] >> 4];
        encoded[index * 2 + 1] = digits[bytes[index] & 0x0f];
    }
    return encoded;
}

bool IsCanonicalUuid(const std::string_view value) {
    if (value.size() != 36 || value == "00000000-0000-0000-0000-000000000000") {
        return false;
    }
    for (std::size_t index{}; index < value.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            if (value[index] != '-') {
                return false;
            }
        } else if (!std::isdigit(static_cast<unsigned char>(value[index])) && (value[index] < 'a' || value[index] > 'f')) {
            return false;
        }
    }
    return true;
}

std::optional<DeploymentKind> ParseKind(const OrderedJson& value) {
    if (!value.is_string()) {
        return std::nullopt;
    }
    const auto kind = value.get<std::string>();
    if (kind == "official") {
        return DeploymentKind::kOfficial;
    }
    return kind == "private" ? std::optional{DeploymentKind::kPrivate} : std::nullopt;
}

std::optional<DeploymentDistribution> ParseDistribution(const OrderedJson& value) {
    if (!value.is_string()) return std::nullopt;
    const auto distribution = value.get<std::string>();
    if (distribution == "official") return DeploymentDistribution::kOfficial;
    if (distribution == "customer") return DeploymentDistribution::kCustomer;
    return distribution == "oem" ? std::optional{DeploymentDistribution::kOem} : std::nullopt;
}

bool ValidOemId(const std::string_view oemId) {
    return oemId.size() >= 3 && oemId.size() <= 32 && oemId.front() != '-' && oemId.back() != '-' && !oemId.contains("--") && oemId != "pixels" &&
           oemId != "official" && oemId != "customer" && oemId != "oem" && std::ranges::all_of(oemId, [](const unsigned char character) {
               return std::islower(character) != 0 || std::isdigit(character) != 0 || character == '-';
           });
}

bool ValidReleaseDomain(const DeploymentKind deploymentKind, const DeploymentDistribution distribution, const std::string_view releaseNamespace,
                        const std::optional<std::string>& oemId) {
    switch (distribution) {
        case DeploymentDistribution::kOfficial:
            return deploymentKind == DeploymentKind::kOfficial && releaseNamespace == "pixels.official" && !oemId;
        case DeploymentDistribution::kCustomer:
            return deploymentKind == DeploymentKind::kPrivate && releaseNamespace == "pixels.customer" && !oemId;
        case DeploymentDistribution::kOem:
            return deploymentKind == DeploymentKind::kPrivate && oemId && ValidOemId(*oemId) && releaseNamespace == "oem." + *oemId;
    }
    return false;
}

template <typename Integer>
std::optional<Integer> ReadUnsigned(const OrderedJson& value) {
    if (!value.is_number_unsigned()) {
        return std::nullopt;
    }
    const auto number = value.get<std::uint64_t>();
    if (number > std::numeric_limits<Integer>::max()) {
        return std::nullopt;
    }
    return static_cast<Integer>(number);
}

std::optional<std::int64_t> ReadNonnegativeTime(const OrderedJson& value) {
    if (!value.is_number_integer()) {
        return std::nullopt;
    }
    const auto timestamp = value.get<std::int64_t>();
    return timestamp >= 0 ? std::optional{timestamp} : std::nullopt;
}

std::string EncodeBase64Url(std::span<const std::uint8_t> bytes);

std::optional<std::vector<std::uint8_t>> DecodeBase64Url(const std::string_view encoded) {
    if (encoded.empty() || encoded.find('=') != std::string_view::npos || encoded.size() % 4 == 1) {
        return std::nullopt;
    }
    const auto decode = [](const unsigned char character) -> std::optional<std::uint8_t> {
        if (character >= 'A' && character <= 'Z') return character - 'A';
        if (character >= 'a' && character <= 'z') return character - 'a' + 26;
        if (character >= '0' && character <= '9') return character - '0' + 52;
        if (character == '-') return 62;
        if (character == '_') return 63;
        return std::nullopt;
    };
    std::vector<std::uint8_t> output{};
    output.reserve(encoded.size() * 3 / 4);
    std::uint32_t accumulator{};
    int bits{};
    for (const unsigned char character : encoded) {
        const auto value = decode(character);
        if (!value) return std::nullopt;
        accumulator = (accumulator << 6) | *value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            output.push_back(static_cast<std::uint8_t>((accumulator >> bits) & 0xff));
        }
    }
    if ((bits > 0 && (accumulator & ((1U << bits) - 1)) != 0) || EncodeBase64Url(output) != encoded) {
        return std::nullopt;
    }
    return output;
}

std::string EncodeBase64Url(const std::span<const std::uint8_t> bytes) {
    constexpr std::string_view alphabet{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"};
    std::string encoded{};
    encoded.reserve((bytes.size() * 4 + 2) / 3);
    std::uint32_t accumulator{};
    int bits{};
    for (const auto byte : bytes) {
        accumulator = (accumulator << 8) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            encoded.push_back(alphabet[(accumulator >> bits) & 0x3f]);
        }
    }
    if (bits > 0) {
        encoded.push_back(alphabet[(accumulator << (6 - bits)) & 0x3f]);
    }
    return encoded;
}

px::Result<DecodedWire, DeploymentIdentityError> DecodeWire(const std::string_view wire, const std::string_view expectedPrefix) {
    if (wire.empty() || wire.size() > kMaximumWireBytes) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    const auto firstSeparator = wire.find('.');
    const auto secondSeparator = firstSeparator == std::string_view::npos ? std::string_view::npos : wire.find('.', firstSeparator + 1);
    if (firstSeparator == std::string_view::npos || secondSeparator == std::string_view::npos ||
        wire.find('.', secondSeparator + 1) != std::string_view::npos || wire.substr(0, firstSeparator) != expectedPrefix) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    auto payload = DecodeBase64Url(wire.substr(firstSeparator + 1, secondSeparator - firstSeparator - 1));
    const auto signature = DecodeBase64Url(wire.substr(secondSeparator + 1));
    if (!payload || !signature || signature->size() != 64) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    DecodedWire decoded{.payload = std::move(*payload)};
    std::ranges::copy(*signature, decoded.signature.begin());
    return decoded;
}

bool VerifyEd25519(const std::array<std::uint8_t, 32>& publicKey, const std::span<const std::uint8_t> signature, const std::string_view domain,
                   const std::span<const std::uint8_t> payload) {
    const PublicKeyHandle key{EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr, publicKey.data(), publicKey.size()), EVP_PKEY_free};
    const DigestContextHandle context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    if (!key || !context || EVP_DigestVerifyInit(context.get(), nullptr, nullptr, nullptr, key.get()) != 1) {
        return false;
    }
    std::vector<std::uint8_t> message{};
    message.reserve(domain.size() + payload.size());
    message.insert(message.end(), domain.begin(), domain.end());
    message.insert(message.end(), payload.begin(), payload.end());
    return EVP_DigestVerify(context.get(), signature.data(), signature.size(), message.data(), message.size()) == 1;
}

px::Result<OrderedJson, DeploymentIdentityError> DecodeSignedJson(const std::string_view wire, const std::string_view prefix,
                                                                  const std::string_view domain, const std::array<std::uint8_t, 32>& publicKey) {
    auto decoded = DecodeWire(wire, prefix);
    if (!decoded) return std::unexpected{decoded.error()};
    if (!VerifyEd25519(publicKey, decoded->signature, domain, decoded->payload)) {
        return std::unexpected{DeploymentIdentityError::kSignature};
    }
    try {
        const std::string payload{decoded->payload.begin(), decoded->payload.end()};
        auto parsed = OrderedJson::parse(payload);
        if (parsed.dump() != payload) {
            return std::unexpected{DeploymentIdentityError::kInvalid};
        }
        return parsed;
    } catch (const std::exception&) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
}

std::optional<std::string> ReadString(const OrderedJson& value) { return value.is_string() ? std::optional{value.get<std::string>()} : std::nullopt; }

px::Result<Certificate, DeploymentIdentityError> ParseCertificate(const OrderedJson& value) {
    if (!HasExactOrderedFields(value, kCertificateFields) || ReadUnsigned<std::uint16_t>(value["schema_version"]) != 2) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    const auto deploymentId = ReadString(value["deployment_id"]);
    const auto deploymentKind = ParseKind(value["deployment_kind"]);
    const auto distribution = ParseDistribution(value["distribution"]);
    const auto releaseNamespace = ReadString(value["release_namespace"]);
    const auto oemId = value["oem_id"].is_null() ? std::optional<std::string>{} : ReadString(value["oem_id"]);
    const auto publicKeyHex = ReadString(value["deployment_public_key_hex"]);
    const auto publicKey = publicKeyHex ? DecodeLowerHex<32>(*publicKeyHex) : std::nullopt;
    const auto certificateVersion = ReadUnsigned<std::uint64_t>(value["certificate_version"]);
    const auto notBefore = ReadNonnegativeTime(value["not_before"]);
    const auto expiresAt = ReadNonnegativeTime(value["expires_at"]);
    const auto issuerKeyId = ReadString(value["issuer_key_id"]);
    if ((!value["oem_id"].is_null() && !value["oem_id"].is_string()) || !deploymentId || !IsCanonicalUuid(*deploymentId) || !deploymentKind ||
        !distribution || !releaseNamespace || !ValidReleaseDomain(*deploymentKind, *distribution, *releaseNamespace, oemId) || !publicKey ||
        std::ranges::all_of(*publicKey, [](const auto byte) { return byte == 0; }) || !certificateVersion || *certificateVersion == 0 || !notBefore ||
        !expiresAt || *expiresAt <= *notBefore || *expiresAt > 253'402'300'799 || !issuerKeyId || !IsLowerHex(*issuerKeyId, 64)) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    return Certificate{.deploymentId = *deploymentId,
                       .deploymentKind = *deploymentKind,
                       .distribution = *distribution,
                       .releaseNamespace = *releaseNamespace,
                       .oemId = oemId,
                       .deploymentPublicKey = *publicKey,
                       .certificateVersion = *certificateVersion,
                       .notBefore = *notBefore,
                       .expiresAt = *expiresAt,
                       .issuerKeyId = *issuerKeyId};
}

bool ValidToken(const std::string_view value) {
    return !value.empty() && value.size() <= 32 && std::ranges::all_of(value, [](const unsigned char character) {
        return std::islower(character) != 0 || std::isdigit(character) != 0 || character == '.' || character == '-' || character == '_';
    });
}

bool ValidSortedStringArray(const OrderedJson& value, const std::size_t maximum, const bool tokenValues) {
    if (!value.is_array() || value.empty() || value.size() > maximum) return false;
    std::string previous{};
    for (const auto& entry : value) {
        const auto current = ReadString(entry);
        if (!current || (tokenValues && !ValidToken(*current)) || (!previous.empty() && previous >= *current)) return false;
        previous = *current;
    }
    return true;
}

px::Result<Descriptor, DeploymentIdentityError> ParseDescriptor(const OrderedJson& value) {
    if (!HasExactDescriptorFields(value) || ReadUnsigned<std::uint16_t>(value["schema_version"]) != 2) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    const auto deploymentId = ReadString(value["deployment_id"]);
    const auto deploymentKind = ParseKind(value["deployment_kind"]);
    const auto distribution = ParseDistribution(value["distribution"]);
    const auto releaseNamespace = ReadString(value["release_namespace"]);
    const auto oemId = value["oem_id"].is_null() ? std::optional<std::string>{} : ReadString(value["oem_id"]);
    const auto descriptorRevision = ReadUnsigned<std::uint64_t>(value["descriptor_revision"]);
    const auto trustEpoch = ReadUnsigned<std::uint64_t>(value["trust_epoch"]);
    const auto issuedAt = ReadNonnegativeTime(value["issued_at"]);
    const auto expiresAt = ReadNonnegativeTime(value["expires_at"]);
    const auto minimumClientBuild = ReadUnsigned<std::uint64_t>(value["minimum_client_build"]);
    const auto minimumProtocolVersion = ReadUnsigned<std::uint16_t>(value["minimum_protocol_version"]);
    const auto maximumProtocolVersion = ReadUnsigned<std::uint16_t>(value["maximum_protocol_version"]);
    const auto registrationPolicy = ReadString(value["registration_policy"]);
    if ((!value["oem_id"].is_null() && !value["oem_id"].is_string()) || !deploymentId || !IsCanonicalUuid(*deploymentId) || !deploymentKind ||
        !distribution || !releaseNamespace || !ValidReleaseDomain(*deploymentKind, *distribution, *releaseNamespace, oemId) || !descriptorRevision ||
        *descriptorRevision == 0 || !trustEpoch || *trustEpoch == 0 || !issuedAt || !expiresAt || *expiresAt <= *issuedAt ||
        *expiresAt - *issuedAt > 86'400 || !minimumClientBuild || !minimumProtocolVersion || *minimumProtocolVersion == 0 ||
        !maximumProtocolVersion || *maximumProtocolVersion < *minimumProtocolVersion || !ValidSortedStringArray(value["api_versions"], 16, true) ||
        !ValidSortedStringArray(value["authentication_methods"], 8, true) || (registrationPolicy != "closed" && registrationPolicy != "open") ||
        value["console_api_path"] != "/api/console" || value["node_control_path"] != "/api/console/node-control") {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    return Descriptor{.deploymentId = *deploymentId,
                      .deploymentKind = *deploymentKind,
                      .distribution = *distribution,
                      .releaseNamespace = *releaseNamespace,
                      .oemId = oemId,
                      .descriptorRevision = *descriptorRevision,
                      .trustEpoch = *trustEpoch,
                      .issuedAt = *issuedAt,
                      .expiresAt = *expiresAt,
                      .minimumClientBuild = *minimumClientBuild,
                      .minimumProtocolVersion = *minimumProtocolVersion,
                      .maximumProtocolVersion = *maximumProtocolVersion};
}

std::array<std::uint8_t, SHA256_DIGEST_LENGTH> Sha256(const std::span<const std::uint8_t> bytes) {
    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> digest{};
    if (SHA256(bytes.data(), bytes.size(), digest.data()) != digest.data()) {
        digest.fill(0);
    }
    return digest;
}

std::int64_t UnixTimeNow() { return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count(); }

}  // namespace

px::Result<DeploymentTrustStore, DeploymentIdentityError> ParseDeploymentTrustStore(const std::string_view canonicalJson) {
    if (canonicalJson.empty() || canonicalJson.size() > 64 * 1024) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    try {
        const auto value = OrderedJson::parse(canonicalJson);
        constexpr std::array<std::string_view, 3> fields{"schema_version", "trust_epoch", "trusted_keys"};
        if (value.dump() != canonicalJson || !HasExactOrderedFields(value, fields) || ReadUnsigned<std::uint16_t>(value["schema_version"]) != 1) {
            return std::unexpected{DeploymentIdentityError::kInvalid};
        }
        const auto trustEpoch = ReadUnsigned<std::uint64_t>(value["trust_epoch"]);
        if (!trustEpoch || *trustEpoch == 0 || !value["trusted_keys"].is_array() || value["trusted_keys"].empty() ||
            value["trusted_keys"].size() > 16) {
            return std::unexpected{DeploymentIdentityError::kInvalid};
        }
        DeploymentTrustStore store{.trustEpoch = *trustEpoch};
        std::string previousKeyId{};
        constexpr std::array<std::string_view, 2> keyFields{"key_id", "public_key_hex"};
        for (const auto& entry : value["trusted_keys"]) {
            if (!HasExactOrderedFields(entry, keyFields)) return std::unexpected{DeploymentIdentityError::kInvalid};
            const auto keyId = ReadString(entry["key_id"]);
            const auto publicKeyHex = ReadString(entry["public_key_hex"]);
            const auto publicKey = publicKeyHex ? DecodeLowerHex<32>(*publicKeyHex) : std::nullopt;
            const auto publicKeyDigest = publicKey ? Sha256(*publicKey) : std::array<std::uint8_t, SHA256_DIGEST_LENGTH>{};
            if (!keyId || !IsLowerHex(*keyId, 64) || !publicKey || std::ranges::all_of(*publicKey, [](const auto byte) { return byte == 0; }) ||
                (!previousKeyId.empty() && previousKeyId >= *keyId) || EncodeLowerHex(publicKeyDigest) != *keyId) {
                return std::unexpected{DeploymentIdentityError::kUntrusted};
            }
            previousKeyId = *keyId;
            store.trustedKeys.push_back(TrustedVendorKey{.keyId = *keyId, .publicKey = *publicKey});
        }
        return store;
    } catch (const std::exception&) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
}

px::Result<VerifiedDeploymentIdentity, DeploymentIdentityError> VerifyDeploymentIdentity(const std::string_view identityJson,
                                                                                         const DeploymentTrustStore& trustStore,
                                                                                         const DeploymentVerificationPolicy& policy,
                                                                                         const std::int64_t now) {
    if (identityJson.empty() || identityJson.size() > 64 * 1024 || trustStore.trustEpoch == 0 || trustStore.trustEpoch != policy.minimumTrustEpoch ||
        trustStore.trustedKeys.empty() || policy.minimumCertificateVersion == 0 || policy.minimumDescriptorRevision == 0 ||
        policy.minimumTrustEpoch == 0 || policy.clientBuild == 0 || policy.protocolVersion == 0 || now < 0 ||
        !ValidReleaseDomain(policy.expectedKind, policy.expectedDistribution, policy.expectedReleaseNamespace, policy.expectedOemId)) {
        return std::unexpected{DeploymentIdentityError::kRejected};
    }
    try {
        const auto identity = OrderedJson::parse(identityJson);
        constexpr std::array<std::string_view, 2> identityFields{"certificate_wire", "descriptor_wire"};
        if (!HasExactOrderedFields(identity, identityFields)) return std::unexpected{DeploymentIdentityError::kInvalid};
        const auto certificateWire = ReadString(identity["certificate_wire"]);
        const auto descriptorWire = ReadString(identity["descriptor_wire"]);
        if (!certificateWire || !descriptorWire) return std::unexpected{DeploymentIdentityError::kInvalid};

        auto decodedCertificate = DecodeWire(*certificateWire, kCertificatePrefix);
        if (!decodedCertificate) return std::unexpected{decodedCertificate.error()};
        const std::string certificatePayload{decodedCertificate->payload.begin(), decodedCertificate->payload.end()};
        auto untrustedCertificateJson = OrderedJson::parse(certificatePayload);
        if (untrustedCertificateJson.dump() != certificatePayload) return std::unexpected{DeploymentIdentityError::kInvalid};
        auto certificate = ParseCertificate(untrustedCertificateJson);
        if (!certificate) return std::unexpected{certificate.error()};
        const auto vendorKey = std::ranges::find(trustStore.trustedKeys, certificate->issuerKeyId, &TrustedVendorKey::keyId);
        if (vendorKey == trustStore.trustedKeys.end()) return std::unexpected{DeploymentIdentityError::kUntrusted};
        if (!VerifyEd25519(vendorKey->publicKey, decodedCertificate->signature, kCertificateDomain, decodedCertificate->payload)) {
            return std::unexpected{DeploymentIdentityError::kSignature};
        }
        if ((policy.expectedDeploymentId && *policy.expectedDeploymentId != certificate->deploymentId) ||
            certificate->deploymentKind != policy.expectedKind || certificate->distribution != policy.expectedDistribution ||
            certificate->releaseNamespace != policy.expectedReleaseNamespace || certificate->oemId != policy.expectedOemId ||
            certificate->certificateVersion < policy.minimumCertificateVersion || certificate->notBefore > now || certificate->expiresAt <= now) {
            return std::unexpected{DeploymentIdentityError::kRejected};
        }

        auto descriptorJson = DecodeSignedJson(*descriptorWire, kDescriptorPrefix, kDescriptorDomain, certificate->deploymentPublicKey);
        if (!descriptorJson) return std::unexpected{descriptorJson.error()};
        auto descriptor = ParseDescriptor(*descriptorJson);
        if (!descriptor) return std::unexpected{descriptor.error()};
        if (descriptor->deploymentId != certificate->deploymentId || descriptor->deploymentKind != certificate->deploymentKind ||
            descriptor->distribution != certificate->distribution || descriptor->releaseNamespace != certificate->releaseNamespace ||
            descriptor->oemId != certificate->oemId || descriptor->descriptorRevision < policy.minimumDescriptorRevision ||
            descriptor->trustEpoch < policy.minimumTrustEpoch || descriptor->issuedAt > now || descriptor->expiresAt <= now ||
            descriptor->minimumClientBuild > policy.clientBuild || policy.protocolVersion < descriptor->minimumProtocolVersion ||
            policy.protocolVersion > descriptor->maximumProtocolVersion) {
            return std::unexpected{DeploymentIdentityError::kRejected};
        }
        return VerifiedDeploymentIdentity{.deploymentId = certificate->deploymentId,
                                          .deploymentKind = certificate->deploymentKind,
                                          .distribution = certificate->distribution,
                                          .releaseNamespace = certificate->releaseNamespace,
                                          .oemId = certificate->oemId,
                                          .deploymentPublicKey = certificate->deploymentPublicKey,
                                          .certificateVersion = certificate->certificateVersion,
                                          .descriptorRevision = descriptor->descriptorRevision,
                                          .trustEpoch = descriptor->trustEpoch,
                                          .descriptorExpiresAt = descriptor->expiresAt};
    } catch (const std::exception&) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
}

px::Result<bool, DeploymentIdentityError> VerifyDeploymentChallenge(const VerifiedDeploymentIdentity& identity, const std::string_view proofWire,
                                                                    const std::string_view expectedNonce, const std::int64_t now) {
    const auto decodedNonce = DecodeBase64Url(expectedNonce);
    if (!decodedNonce || decodedNonce->size() != 32 || identity.deploymentId.empty() || identity.descriptorRevision == 0 || now < 0) {
        return std::unexpected{DeploymentIdentityError::kRejected};
    }
    auto challengeJson = DecodeSignedJson(proofWire, kChallengePrefix, kChallengeDomain, identity.deploymentPublicKey);
    if (!challengeJson) return std::unexpected{challengeJson.error()};
    if (!HasExactOrderedFields(*challengeJson, kChallengeFields) || ReadUnsigned<std::uint16_t>((*challengeJson)["schema_version"]) != 1) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    const auto deploymentId = ReadString((*challengeJson)["deployment_id"]);
    const auto descriptorRevision = ReadUnsigned<std::uint64_t>((*challengeJson)["descriptor_revision"]);
    const auto nonce = ReadString((*challengeJson)["nonce"]);
    const auto issuedAt = ReadNonnegativeTime((*challengeJson)["issued_at"]);
    const auto expiresAt = ReadNonnegativeTime((*challengeJson)["expires_at"]);
    if (!deploymentId || *deploymentId != identity.deploymentId || !descriptorRevision || *descriptorRevision != identity.descriptorRevision ||
        !nonce || *nonce != expectedNonce || !issuedAt || !expiresAt || *expiresAt <= *issuedAt || *expiresAt - *issuedAt > 60 || *issuedAt > now ||
        *expiresAt <= now) {
        return std::unexpected{DeploymentIdentityError::kRejected};
    }
    return true;
}

px::Result<std::string, DeploymentIdentityError> GenerateDeploymentChallengeNonce() {
    std::array<std::uint8_t, 32> nonce{};
    if (RAND_bytes(nonce.data(), static_cast<int>(nonce.size())) != 1) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
    return EncodeBase64Url(nonce);
}

px::Result<VerifiedDeploymentIdentity, DeploymentIdentityError> VerifyConsoleDeployment(const std::string& host, const int port,
                                                                                        const DeploymentTrustStore& trustStore,
                                                                                        const DeploymentVerificationPolicy& policy) {
    if (host.empty() || port <= 0 || port > 65'535) return std::unexpected{DeploymentIdentityError::kRejected};
    const auto identityClient = MakeConsoleHttpClient(host, port, "/.well-known/pixels", 5'000);
    identityClient->SetResponseBodyLimit(64 * 1024);
    const auto identityResponse = identityClient->Request();
    if (identityResponse.status != 200 || identityResponse.error_code != 0 || identityResponse.body.empty()) {
        return std::unexpected{DeploymentIdentityError::kRejected};
    }
    auto identity = VerifyDeploymentIdentity(identityResponse.body, trustStore, policy, UnixTimeNow());
    if (!identity) return std::unexpected{identity.error()};
    const auto nonce = GenerateDeploymentChallengeNonce();
    if (!nonce) return std::unexpected{nonce.error()};

    const auto challengeClient = MakeConsoleHttpClient(host, port, "/.well-known/pixels/challenge", 5'000);
    challengeClient->SetResponseBodyLimit(64 * 1024);
    const OrderedJson challengeRequest{{"nonce", *nonce}, {"descriptor_revision", identity->descriptorRevision}};
    const auto challengeResponse = challengeClient->Post({}, challengeRequest.dump(), "application/json");
    if (challengeResponse.status != 200 || challengeResponse.error_code != 0 || challengeResponse.body.empty()) {
        return std::unexpected{DeploymentIdentityError::kRejected};
    }
    try {
        const auto proof = OrderedJson::parse(challengeResponse.body);
        constexpr std::array<std::string_view, 1> proofFields{"proof_wire"};
        if (!HasExactOrderedFields(proof, proofFields)) return std::unexpected{DeploymentIdentityError::kInvalid};
        const auto proofWire = ReadString(proof["proof_wire"]);
        if (!proofWire) return std::unexpected{DeploymentIdentityError::kInvalid};
        const auto verified = VerifyDeploymentChallenge(*identity, *proofWire, *nonce, UnixTimeNow());
        if (!verified) return std::unexpected{verified.error()};
        return identity;
    } catch (const std::exception&) {
        return std::unexpected{DeploymentIdentityError::kInvalid};
    }
}

}  // namespace px_console
