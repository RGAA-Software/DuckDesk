#include "rdp_proxy_policy.h"

#include <algorithm>
#include <array>
#include <memory>
#include <vector>

#ifdef _WIN32
#include <Windows.h>
#include <wincrypt.h>
#endif

namespace px::rdp {
namespace {
unsigned char Lower(unsigned char value) {
    return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value + ('a' - 'A')) : value;
}

bool EqualAscii(std::string_view left, std::string_view right) {
    return left.size() == right.size() && std::ranges::equal(left, right, [](unsigned char a, unsigned char b) { return Lower(a) == Lower(b); });
}

bool IsHex(unsigned char value) {
    return (value >= '0' && value <= '9') || (Lower(value) >= 'a' && Lower(value) <= 'f');
}
} // namespace

bool IsWorkspacePeer(std::string_view expected_user, std::string_view expected_domain, std::string_view user, std::string_view domain) {
    return expected_user.starts_with("grdp_") && expected_user.size() >= 8 && expected_user.size() <= 20 && !expected_domain.empty() &&
           expected_domain.size() <= 255 && EqualAscii(expected_user, user) && EqualAscii(expected_domain, domain);
}

bool IsAllowedStaticChannel(std::string_view name) {
    return name == "drdynvc" || name == "cliprdr" || name == "rdpsnd" || name == "rdpdr";
}

bool IsAllowedDynamicChannel(std::string_view name) {
    return name == "Microsoft::Windows::RDS::Graphics" || name == "Microsoft::Windows::RDS::DisplayControl" || name == "AUDIO_PLAYBACK_DVC" ||
           name == "AUDIO_PLAYBACK_LOSSY_DVC";
}

bool IsAudioDeviceHandshake(DeviceChannelDirection direction, std::span<const unsigned char> bytes, std::uint32_t flags, std::size_t total_size) {
    // FIRST | LAST, optionally SHOW_PROTOCOL. Reject fragmentation/compression;
    // the supported handshake fits a single standard virtual-channel chunk.
    if (bytes.size() < 4 || bytes.size() > 1024 || total_size != bytes.size() || (flags & 3) != 3 || (flags & ~0x13u) != 0) {
        return false;
    }
    const auto u16 = [bytes](std::size_t offset) { return std::uint32_t{bytes[offset]} | (std::uint32_t{bytes[offset + 1]} << 8); };
    const auto u32 = [u16](std::size_t offset) { return u16(offset) | (u16(offset + 2) << 16); };
    if (u16(0) != 0x4472) {
        return false;
    }
    const auto packet = u16(2);
    const bool client = direction == DeviceChannelDirection::kClientToServer;
    if (packet == 0x4343 || (!client && packet == 0x496e)) {
        return bytes.size() == 12 && u16(4) == 1;
    }
    if (!client && packet == 0x554c) {
        return bytes.size() == 4;
    }
    if (client && (packet == 0x4441 || packet == 0x444d)) {
        return bytes.size() == 8 && u32(4) == 0;
    }
    if (client && packet == 0x434e) {
        if (bytes.size() < 18 || u32(4) != 1 || u32(8) != 0) {
            return false;
        }
        const auto length = u32(12);
        return length >= 2 && length <= 512 && length % 2 == 0 && length == bytes.size() - 16 && u16(bytes.size() - 2) == 0;
    }
    if ((client && packet == 0x4350) || (!client && packet == 0x5350)) {
        if (bytes.size() < 8 || u16(4) == 0 || u16(4) > 5 || u16(6) != 0) {
            return false;
        }
        std::size_t offset{8};
        std::uint32_t seen{};
        for (std::uint32_t index{}; index < u16(4); ++index) {
            if (bytes.size() - offset < 8) {
                return false;
            }
            const auto type = u16(offset);
            const auto length = u16(offset + 2);
            if (type < 1 || type > 5 || (seen & (1u << type)) || length < 8 || length > bytes.size() - offset) {
                return false;
            }
            seen |= 1u << type;
            offset += length;
        }
        return offset == bytes.size() && (seen & 2) != 0;
    }
    return false;
}

#ifdef _WIN32
namespace {
struct CertificateCloser final {
    void operator()(const CERT_CONTEXT* certificate) const noexcept { // NOLINT(gammaray-raw-pointer-boundary): WinCrypt RAII deleter ABI.
        if (certificate) {
            CertFreeCertificateContext(certificate);
        }
    }
};
using Certificate = std::unique_ptr<const CERT_CONTEXT, CertificateCloser>;
} // namespace

bool VerifyPinnedCertificate(std::span<const unsigned char> pem, std::string_view sha256_hex) {
    if (pem.empty() || pem.size() > 64 * 1024 || sha256_hex.size() != 64 || !std::ranges::all_of(sha256_hex, IsHex)) {
        return false;
    }
    DWORD length{};
    if (!CryptStringToBinaryA(reinterpret_cast<const char*>(pem.data()), static_cast<DWORD>(pem.size()), CRYPT_STRING_BASE64HEADER, nullptr, &length,
                              nullptr, nullptr) ||
        length == 0 || length > 64 * 1024) {
        return false;
    }
    auto der = std::vector<unsigned char>(length);
    if (!CryptStringToBinaryA(reinterpret_cast<const char*>(pem.data()), static_cast<DWORD>(pem.size()), CRYPT_STRING_BASE64HEADER, der.data(),
                              &length, nullptr, nullptr)) {
        return false;
    }
    const auto certificate = Certificate{CertCreateCertificateContext(X509_ASN_ENCODING, der.data(), length)};
    if (!certificate || CertVerifyTimeValidity(nullptr, certificate->pCertInfo) != 0) {
        return false;
    }
    std::array<unsigned char, 32> digest{};
    DWORD digest_size{static_cast<DWORD>(digest.size())};
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, der.data(), length, digest.data(), &digest_size) ||
        digest_size != digest.size()) {
        return false;
    }
    constexpr std::string_view hex{"0123456789abcdef"};
    unsigned char difference{};
    for (std::size_t index{}; index < digest.size(); ++index) {
        difference |= static_cast<unsigned char>(hex[digest[index] >> 4]) ^ Lower(static_cast<unsigned char>(sha256_hex[index * 2]));
        difference |= static_cast<unsigned char>(hex[digest[index] & 15]) ^ Lower(static_cast<unsigned char>(sha256_hex[index * 2 + 1]));
    }
    return difference == 0;
}
#endif

} // namespace px::rdp
