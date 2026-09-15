#include "rdp_private_directory.h"

#ifdef _WIN32
#include <algorithm>
#include <memory>
#include <optional>
#include <vector>
#include <aclapi.h>
#include <sddl.h>

namespace px::rdp {
namespace {
struct SecurityDescriptorCloser final {
    void operator()(void* descriptor) const noexcept { // NOLINT(pixels-raw-pointer-boundary): LocalFree Win32 security descriptor ABI.
        if (descriptor) {
            LocalFree(descriptor);
        }
    }
};
using Descriptor = std::unique_ptr<void, SecurityDescriptorCloser>;

std::optional<std::vector<std::vector<unsigned char>>> CanonicalAclEntries(const Descriptor& descriptor) {
    SECURITY_DESCRIPTOR_CONTROL control{};
    DWORD revision{};
    if (!GetSecurityDescriptorControl(descriptor.get(), &control, &revision) || !(control & SE_DACL_PROTECTED)) {
        return std::nullopt;
    }
    PACL acl{}; // NOLINT(pixels-raw-pointer-boundary): synchronous Win32 borrowed out value, immediately copied into a value buffer.
    BOOL present{};
    BOOL defaulted{};
    if (!GetSecurityDescriptorDacl(descriptor.get(), &present, &acl, &defaulted) || !present || !acl || !IsValidAcl(acl)) {
        return std::nullopt;
    }
    std::vector<std::vector<unsigned char>> entries{};
    entries.reserve(acl->AceCount);
    for (DWORD index{}; index < acl->AceCount; ++index) {
        void* ace{}; // NOLINT(pixels-raw-pointer-boundary): synchronous GetAce borrowed ABI, copied before the next call.
        if (!GetAce(acl, index, &ace) || !ace) {
            return std::nullopt;
        }
        const auto& header = *static_cast<const ACE_HEADER*>(ace);
        if (header.AceSize < sizeof(ACE_HEADER)) {
            return std::nullopt;
        }
        const auto* begin = static_cast<const unsigned char*>(ace); // NOLINT(pixels-raw-pointer-boundary): transient borrowed ABI range.
        entries.emplace_back(begin, begin + header.AceSize);
    }
    std::ranges::sort(entries);
    return entries;
}

bool TrustedOwner(const Descriptor& descriptor) {
    PSID owner{}; // NOLINT(pixels-raw-pointer-boundary): borrowed Win32 out value, used synchronously only.
    BOOL defaulted{};
    if (!GetSecurityDescriptorOwner(descriptor.get(), &owner, &defaulted) || !owner || !IsValidSid(owner)) {
        return false;
    }
    if (IsWellKnownSid(owner, WinLocalSystemSid) || IsWellKnownSid(owner, WinBuiltinAdministratorsSid)) {
        return true;
    }
    HANDLE token_output{}; // NOLINT(pixels-raw-pointer-boundary): Win32 out handle, immediately smart-owned.
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token_output)) {
        return false;
    }
    const auto token = UniqueWinHandle{token_output};
    DWORD size{};
    if (GetTokenInformation(token.get(), TokenUser, nullptr, 0, &size) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || size > 64 * 1024) {
        return false;
    }
    auto bytes = std::vector<unsigned char>(size);
    if (!GetTokenInformation(token.get(), TokenUser, bytes.data(), size, &size)) {
        return false;
    }
    const auto& user = *reinterpret_cast<const TOKEN_USER*>(bytes.data());
    return EqualSid(owner, user.User.Sid) != FALSE;
}
} // namespace

UniqueWinHandle OpenPrivateRdpDirectory(const std::filesystem::path& path, bool create) {
    if (!path.is_absolute()) {
        return {};
    }
    PSECURITY_DESCRIPTOR expected_output{}; // NOLINT(pixels-raw-pointer-boundary): Win32 out allocation, immediately RAII-wrapped.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(L"D:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)", SDDL_REVISION_1, &expected_output, nullptr)) {
        return {};
    }
    const auto expected = Descriptor{expected_output};
    if (create) {
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.lpSecurityDescriptor = expected.get();
        if (!CreateDirectoryW(path.c_str(), &attributes) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return {};
        }
    }
    auto directory = UniqueWinHandle{CreateFileW(path.c_str(), READ_CONTROL, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                                 FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr)};
    if (!directory || directory.get() == INVALID_HANDLE_VALUE) {
        return {};
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(directory.get(), &info) || !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ||
        (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        return {};
    }
    PSECURITY_DESCRIPTOR actual_output{}; // NOLINT(pixels-raw-pointer-boundary): Win32 out allocation, immediately RAII-wrapped.
    if (GetSecurityInfo(directory.get(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION | OWNER_SECURITY_INFORMATION, nullptr, nullptr, nullptr, nullptr,
                        &actual_output) != ERROR_SUCCESS) {
        return {};
    }
    const auto actual = Descriptor{actual_output};
    const auto expected_acl = CanonicalAclEntries(expected);
    if (!expected_acl || CanonicalAclEntries(actual) != expected_acl || !TrustedOwner(actual)) {
        return {};
    }
    return directory;
}

} // namespace px::rdp
#endif
