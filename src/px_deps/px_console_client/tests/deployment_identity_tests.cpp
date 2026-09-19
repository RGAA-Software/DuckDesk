#include <gtest/gtest.h>
#include <openssl/evp.h>
#include <openssl/sha.h>

#include <array>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "px_console_client/deployment_identity.h"

namespace px_console {
namespace {

using OrderedJson = nlohmann::ordered_json;
using KeyHandle = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using DigestContextHandle = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;

constexpr std::int64_t kNow{1'700'000'000};
constexpr char kCertificateDomainBytes[] = "Pixels-Deployment-Certificate-v1\0";
constexpr char kDescriptorDomainBytes[] = "Pixels-Platform-Descriptor-v1\0";
constexpr char kChallengeDomainBytes[] = "Pixels-Deployment-Challenge-v1\0";

std::string Base64Url(const std::span<const std::uint8_t> bytes) {
    constexpr std::string_view alphabet{"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_"};
    std::string encoded{};
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
    if (bits > 0) encoded.push_back(alphabet[(accumulator << (6 - bits)) & 0x3f]);
    return encoded;
}

std::string LowerHex(const std::span<const std::uint8_t> bytes) {
    constexpr std::string_view digits{"0123456789abcdef"};
    std::string encoded(bytes.size() * 2, '\0');
    for (std::size_t index{}; index < bytes.size(); ++index) {
        encoded[index * 2] = digits[bytes[index] >> 4];
        encoded[index * 2 + 1] = digits[bytes[index] & 0x0f];
    }
    return encoded;
}

KeyHandle PrivateKey(const std::array<std::uint8_t, 32>& seed) {
    return KeyHandle{EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr, seed.data(), seed.size()), EVP_PKEY_free};
}

std::array<std::uint8_t, 32> PublicKey(const KeyHandle& key) {
    std::array<std::uint8_t, 32> publicKey{};
    std::size_t publicKeySize{publicKey.size()};
    EXPECT_EQ(EVP_PKEY_get_raw_public_key(key.get(), publicKey.data(), &publicKeySize), 1);
    EXPECT_EQ(publicKeySize, publicKey.size());
    return publicKey;
}

std::string KeyId(const std::array<std::uint8_t, 32>& publicKey) {
    std::array<std::uint8_t, SHA256_DIGEST_LENGTH> digest{};
    EXPECT_EQ(SHA256(publicKey.data(), publicKey.size(), digest.data()), digest.data());
    return LowerHex(digest);
}

std::string SignedWire(const std::string_view prefix, const std::string_view domain, const OrderedJson& payload, const KeyHandle& key) {
    const auto payloadText = payload.dump();
    std::vector<std::uint8_t> message{};
    message.insert(message.end(), domain.begin(), domain.end());
    message.insert(message.end(), payloadText.begin(), payloadText.end());
    const DigestContextHandle context{EVP_MD_CTX_new(), EVP_MD_CTX_free};
    EXPECT_TRUE(context);
    EXPECT_EQ(EVP_DigestSignInit(context.get(), nullptr, nullptr, nullptr, key.get()), 1);
    std::array<std::uint8_t, 64> signature{};
    std::size_t signatureSize{signature.size()};
    EXPECT_EQ(EVP_DigestSign(context.get(), signature.data(), &signatureSize, message.data(), message.size()), 1);
    EXPECT_EQ(signatureSize, signature.size());
    const std::vector<std::uint8_t> payloadBytes{payloadText.begin(), payloadText.end()};
    return std::string{prefix} + "." + Base64Url(payloadBytes) + "." + Base64Url(signature);
}

struct IdentityFixture final {
    std::string trustStoreJson{};
    std::string identityJson{};
    std::string challengeWire{};
    std::string nonce{};
};

IdentityFixture MakeFixture() {
    std::array<std::uint8_t, 32> vendorSeed{};
    std::array<std::uint8_t, 32> deploymentSeed{};
    for (std::size_t index{}; index < vendorSeed.size(); ++index) {
        vendorSeed[index] = static_cast<std::uint8_t>(index + 1);
        deploymentSeed[index] = static_cast<std::uint8_t>(index + 65);
    }
    const auto vendorKey = PrivateKey(vendorSeed);
    const auto deploymentKey = PrivateKey(deploymentSeed);
    EXPECT_TRUE(vendorKey);
    EXPECT_TRUE(deploymentKey);
    const auto vendorPublicKey = PublicKey(vendorKey);
    const auto deploymentPublicKey = PublicKey(deploymentKey);
    const auto vendorKeyId = KeyId(vendorPublicKey);
    const std::string deploymentId{"8f9cbade-f2c1-47d4-a92e-109675684b21"};

    const OrderedJson certificate{{"schema_version", 1},          {"deployment_id", deploymentId},
                                  {"deployment_kind", "private"}, {"deployment_public_key_hex", LowerHex(deploymentPublicKey)},
                                  {"certificate_version", 4},     {"not_before", kNow - 60},
                                  {"expires_at", kNow + 86'400},  {"issuer_key_id", vendorKeyId}};
    const OrderedJson descriptor{{"schema_version", 1},
                                 {"deployment_id", deploymentId},
                                 {"deployment_kind", "private"},
                                 {"descriptor_revision", 7},
                                 {"trust_epoch", 3},
                                 {"issued_at", kNow - 10},
                                 {"expires_at", kNow + 600},
                                 {"minimum_client_build", 25},
                                 {"api_versions", OrderedJson::array({"console.v1", "node.v1"})},
                                 {"minimum_protocol_version", 1},
                                 {"maximum_protocol_version", 2},
                                 {"authentication_methods", OrderedJson::array({"guest", "password"})},
                                 {"registration_policy", "closed"},
                                 {"console_api_path", "/api/console"},
                                 {"node_control_path", "/api/console/node-control"}};
    const auto certificateWire =
        SignedWire("PXDC1", std::string_view{kCertificateDomainBytes, sizeof(kCertificateDomainBytes) - 1}, certificate, vendorKey);
    const auto descriptorWire =
        SignedWire("PXDD1", std::string_view{kDescriptorDomainBytes, sizeof(kDescriptorDomainBytes) - 1}, descriptor, deploymentKey);
    const OrderedJson identity{{"certificate_wire", certificateWire}, {"descriptor_wire", descriptorWire}};

    std::array<std::uint8_t, 32> nonceBytes{};
    nonceBytes.fill(42);
    const auto nonce = Base64Url(nonceBytes);
    const OrderedJson challenge{{"schema_version", 1}, {"deployment_id", deploymentId}, {"descriptor_revision", 7}, {"nonce", nonce},
                                {"issued_at", kNow},   {"expires_at", kNow + 30}};
    const auto challengeWire =
        SignedWire("PXDP1", std::string_view{kChallengeDomainBytes, sizeof(kChallengeDomainBytes) - 1}, challenge, deploymentKey);
    const OrderedJson trustStore{
        {"schema_version", 1},
        {"trust_epoch", 3},
        {"trusted_keys", OrderedJson::array({OrderedJson{{"key_id", vendorKeyId}, {"public_key_hex", LowerHex(vendorPublicKey)}}})}};
    return {.trustStoreJson = trustStore.dump(), .identityJson = identity.dump(), .challengeWire = challengeWire, .nonce = nonce};
}

DeploymentVerificationPolicy PrivatePolicy() {
    return {.expectedDeploymentId = "8f9cbade-f2c1-47d4-a92e-109675684b21",
            .expectedKind = DeploymentKind::kPrivate,
            .minimumCertificateVersion = 4,
            .minimumDescriptorRevision = 7,
            .minimumTrustEpoch = 3,
            .clientBuild = 25,
            .protocolVersion = 2};
}

TEST(DeploymentIdentity, AcceptsMatchingIdentityAndNonceProof) {
    const auto fixture = MakeFixture();
    const auto trustStore = ParseDeploymentTrustStore(fixture.trustStoreJson);
    ASSERT_TRUE(trustStore);
    const auto identity = VerifyDeploymentIdentity(fixture.identityJson, *trustStore, PrivatePolicy(), kNow);
    ASSERT_TRUE(identity);
    EXPECT_EQ(identity->certificateVersion, 4);
    EXPECT_EQ(identity->descriptorRevision, 7);
    EXPECT_TRUE(VerifyDeploymentChallenge(*identity, fixture.challengeWire, fixture.nonce, kNow + 1));
}

TEST(DeploymentIdentity, RejectsWrongFlavorRollbackTamperingAndReplay) {
    const auto fixture = MakeFixture();
    const auto trustStore = ParseDeploymentTrustStore(fixture.trustStoreJson);
    ASSERT_TRUE(trustStore);

    auto wrongFlavor = PrivatePolicy();
    wrongFlavor.expectedKind = DeploymentKind::kOfficial;
    EXPECT_FALSE(VerifyDeploymentIdentity(fixture.identityJson, *trustStore, wrongFlavor, kNow));
    auto staleRevision = PrivatePolicy();
    staleRevision.minimumDescriptorRevision = 8;
    EXPECT_FALSE(VerifyDeploymentIdentity(fixture.identityJson, *trustStore, staleRevision, kNow));

    auto tamperedIdentity = fixture.identityJson;
    tamperedIdentity[tamperedIdentity.find("PXDD1") + 7] = 'A';
    EXPECT_FALSE(VerifyDeploymentIdentity(tamperedIdentity, *trustStore, PrivatePolicy(), kNow));
    const auto identity = VerifyDeploymentIdentity(fixture.identityJson, *trustStore, PrivatePolicy(), kNow);
    ASSERT_TRUE(identity);
    const std::array<std::uint8_t, 32> wrongNonce{};
    EXPECT_FALSE(VerifyDeploymentChallenge(*identity, fixture.challengeWire, Base64Url(wrongNonce), kNow + 1));
    EXPECT_FALSE(VerifyDeploymentChallenge(*identity, fixture.challengeWire, fixture.nonce, kNow + 31));
}

TEST(DeploymentIdentity, TrustStoreRequiresCanonicalOrderedKeysAndCorrectKeyIds) {
    const auto fixture = MakeFixture();
    EXPECT_TRUE(ParseDeploymentTrustStore(fixture.trustStoreJson));
    auto reordered = OrderedJson::parse(fixture.trustStoreJson);
    const OrderedJson nonCanonical{
        {"trust_epoch", reordered["trust_epoch"]}, {"schema_version", reordered["schema_version"]}, {"trusted_keys", reordered["trusted_keys"]}};
    EXPECT_FALSE(ParseDeploymentTrustStore(nonCanonical.dump()));
    reordered["trusted_keys"][0]["key_id"] = std::string(64, 'a');
    EXPECT_FALSE(ParseDeploymentTrustStore(reordered.dump()));
}

}  // namespace
}  // namespace px_console
