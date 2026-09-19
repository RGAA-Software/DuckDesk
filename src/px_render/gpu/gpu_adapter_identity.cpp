#include "gpu_adapter_identity.h"

#include <Windows.h>
#include <bcrypt.h>
#include <d3dkmthk.h>
#include <wincrypt.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <optional>
#include <utility>
#include <vector>

namespace px::gpu {
namespace {

constexpr KMTQUERYADAPTERINFOTYPE kPhysicalAdapterCountQuery = static_cast<KMTQUERYADAPTERINFOTYPE>(30);
constexpr KMTQUERYADAPTERINFOTYPE kPhysicalAdapterPnpKeyQuery = static_cast<KMTQUERYADAPTERINFOTYPE>(41);
constexpr std::size_t kMaximumPnpKeyCharacters = 1024;
constexpr std::string_view kStableKeyPrefix = "pnp-sha256:";

class AdapterHandle final {
public:
    static std::optional<AdapterHandle> Open(LUID adapter_luid) {
        D3DKMT_OPENADAPTERFROMLUID request{};
        request.AdapterLuid = adapter_luid;
        if (D3DKMTOpenAdapterFromLuid(&request) < 0 || request.hAdapter == 0) {
            return std::nullopt;
        }
        return AdapterHandle(request.hAdapter);
    }

    AdapterHandle(const AdapterHandle&) = delete;
    AdapterHandle& operator=(const AdapterHandle&) = delete;

    AdapterHandle(AdapterHandle&& other) noexcept : handle_(std::exchange(other.handle_, 0)) {}

    AdapterHandle& operator=(AdapterHandle&& other) noexcept {
        if (this != &other) {
            Close();
            handle_ = std::exchange(other.handle_, 0);
        }
        return *this;
    }

    ~AdapterHandle() { Close(); }

    [[nodiscard]] D3DKMT_HANDLE Get() const { return handle_; }

private:
    explicit AdapterHandle(D3DKMT_HANDLE handle) : handle_(handle) {}

    void Close() {
        if (handle_ == 0) {
            return;
        }
        D3DKMT_CLOSEADAPTER request{};
        request.hAdapter = std::exchange(handle_, 0);
        (void)D3DKMTCloseAdapter(&request);
    }

    D3DKMT_HANDLE handle_ = 0;
};

std::optional<std::string> CanonicalizePnpIdentity(std::wstring_view identity) {
    std::string canonical;
    canonical.reserve(identity.size());
    for (const wchar_t wide_character : identity) {
        if (wide_character > 0x7f) {
            return std::nullopt;
        }
        const auto character = static_cast<unsigned char>(wide_character == L'/' ? L'\\' : wide_character);
        canonical.push_back(static_cast<char>(std::toupper(character)));
    }
    constexpr std::string_view enum_marker = "\\ENUM\\";
    constexpr std::string_view device_parameters_suffix = "\\DEVICE PARAMETERS";
    if (const auto marker_offset = canonical.find(enum_marker); marker_offset != std::string::npos) {
        canonical.erase(0, marker_offset + enum_marker.size());
    }
    if (canonical.ends_with(device_parameters_suffix)) {
        canonical.erase(canonical.size() - device_parameters_suffix.size());
    }
    if (canonical.empty()) {
        return std::nullopt;
    }
    return canonical;
}

std::optional<std::string> StableKey(std::wstring_view identity) {
    const auto canonical = CanonicalizePnpIdentity(identity);
    if (!canonical) {
        return std::nullopt;
    }
    const std::vector<BYTE> identity_bytes(canonical->begin(), canonical->end());
    std::array<BYTE, 32> digest{};
    DWORD digest_size = static_cast<DWORD>(digest.size());
    if (!CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, identity_bytes.data(), static_cast<DWORD>(identity_bytes.size()), digest.data(),
                               &digest_size) ||
        digest_size != digest.size()) {
        return std::nullopt;
    }
    constexpr std::array<char, 16> hex_digits{'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'};
    std::string stable_key(kStableKeyPrefix);
    stable_key.reserve(kStableKeyPrefix.size() + digest.size() * 2);
    for (const BYTE value : digest) {
        stable_key.push_back(hex_digits[value >> 4]);
        stable_key.push_back(hex_digits[value & 0x0f]);
    }
    return stable_key;
}

std::optional<UINT> PhysicalAdapterCount(D3DKMT_HANDLE adapter_handle) {
    UINT physical_adapter_count = 0;
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = adapter_handle;
    query.Type = kPhysicalAdapterCountQuery;
    query.pPrivateDriverData = &physical_adapter_count;
    query.PrivateDriverDataSize = sizeof(physical_adapter_count);
    if (D3DKMTQueryAdapterInfo(&query) < 0 || physical_adapter_count == 0) {
        return std::nullopt;
    }
    return physical_adapter_count;
}

std::optional<std::string> PhysicalAdapterStableKey(D3DKMT_HANDLE adapter_handle, UINT physical_adapter_index) {
    std::array<WCHAR, kMaximumPnpKeyCharacters> destination{};
    UINT destination_character_count = static_cast<UINT>(destination.size());
    D3DKMT_QUERY_PHYSICAL_ADAPTER_PNP_KEY pnp_key{};
    pnp_key.PhysicalAdapterIndex = physical_adapter_index;
    pnp_key.PnPKeyType = D3DKMT_PNP_KEY_HARDWARE;
    pnp_key.pDest = destination.data();
    pnp_key.pCchDest = &destination_character_count;
    D3DKMT_QUERYADAPTERINFO query{};
    query.hAdapter = adapter_handle;
    query.Type = kPhysicalAdapterPnpKeyQuery;
    query.pPrivateDriverData = &pnp_key;
    query.PrivateDriverDataSize = sizeof(pnp_key);
    if (D3DKMTQueryAdapterInfo(&query) < 0) {
        return std::nullopt;
    }
    const auto terminator = std::ranges::find(destination, L'\0');
    return StableKey(std::wstring_view(destination.begin(), terminator));
}

LUID UnpackAdapterLuid(std::int64_t packed_luid) {
    const auto bits = std::bit_cast<std::uint64_t>(packed_luid);
    return LUID{
        .LowPart = static_cast<DWORD>(bits & 0xffffffffULL),
        .HighPart = std::bit_cast<LONG>(static_cast<std::uint32_t>(bits >> 32)),
    };
}

}  // namespace

std::int64_t PackAdapterLuid(std::uint32_t low_part, std::int32_t high_part) {
    const auto high_bits = static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(high_part)) << 32;
    return std::bit_cast<std::int64_t>(high_bits | low_part);
}

std::vector<std::string> StableKeysForAdapter(std::int64_t packed_luid) {
    auto adapter = AdapterHandle::Open(UnpackAdapterLuid(packed_luid));
    if (!adapter) {
        return {};
    }
    const auto physical_adapter_count = PhysicalAdapterCount(adapter->Get());
    if (!physical_adapter_count) {
        return {};
    }
    std::vector<std::string> stable_keys;
    stable_keys.reserve(*physical_adapter_count);
    for (UINT physical_adapter_index = 0; physical_adapter_index < *physical_adapter_count; ++physical_adapter_index) {
        if (auto stable_key = PhysicalAdapterStableKey(adapter->Get(), physical_adapter_index)) {
            stable_keys.push_back(std::move(*stable_key));
        }
    }
    std::ranges::sort(stable_keys);
    const auto duplicate_begin = std::ranges::unique(stable_keys).begin();
    stable_keys.erase(duplicate_begin, stable_keys.end());
    return stable_keys;
}

bool AdapterMatchesStableKey(std::int64_t packed_luid, std::string_view expected_stable_key) {
    if (!expected_stable_key.starts_with(kStableKeyPrefix) || expected_stable_key.size() != kStableKeyPrefix.size() + 64) {
        return false;
    }
    const auto stable_keys = StableKeysForAdapter(packed_luid);
    return std::ranges::find(stable_keys, expected_stable_key) != stable_keys.end();
}

}  // namespace px::gpu
