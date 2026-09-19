#include "panel_credential_vault.h"

#include <Windows.h>
#include <wincred.h>

#include <utility>

namespace px::panel::product {
namespace {

class CredentialHandle final {
public:
    explicit CredentialHandle(PCREDENTIALW value) : value_{value} {}
    ~CredentialHandle() {
        if (value_) {
            CredFree(value_);
        }
    }
    CredentialHandle(const CredentialHandle&) = delete;
    CredentialHandle& operator=(const CredentialHandle&) = delete;

    [[nodiscard]] const CREDENTIALW& Value() const { return *value_; }

private:
    PCREDENTIALW value_{};  // NOLINT(pixels-raw-pointer-boundary): owned WinCred allocation wrapped immediately
};

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), count) == count
               ? result
               : std::wstring{};
}

}  // namespace

std::shared_ptr<PanelCredentialVault> PanelCredentialVault::Create(std::string credentialNamespace) {
    if (credentialNamespace.empty()) return {};
    return std::make_shared<PanelCredentialVault>(std::move(credentialNamespace));
}

PanelCredentialVault::PanelCredentialVault(std::string credentialNamespace) : credentialNamespace_{std::move(credentialNamespace)} {}

std::optional<std::string> PanelCredentialVault::Read(const std::string& target) const {
    const auto credentialTarget = CredentialTarget(target);
    PCREDENTIALW credential{};  // NOLINT(pixels-raw-pointer-boundary): WinCred output parameter, wrapped on the next statement
    if (credentialTarget.empty() || !CredReadW(credentialTarget.c_str(), CRED_TYPE_GENERIC, 0, &credential)) {
        return std::nullopt;
    }
    const CredentialHandle owned{credential};
    const auto& value = owned.Value();
    if (!value.CredentialBlob || value.CredentialBlobSize == 0) {
        return std::nullopt;
    }
    return std::string{reinterpret_cast<const char*>(value.CredentialBlob), value.CredentialBlobSize};
}

bool PanelCredentialVault::Write(const std::string& target, const std::string& password) const {
    auto credentialTarget = CredentialTarget(target);
    if (credentialTarget.empty() || password.empty() || password.size() > CRED_MAX_CREDENTIAL_BLOB_SIZE) {
        return false;
    }
    CREDENTIALW credential{};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = credentialTarget.data();
    credential.CredentialBlobSize = static_cast<DWORD>(password.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(password.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(L"Pixels remote device");
    return CredWriteW(&credential, 0) != FALSE;
}

void PanelCredentialVault::Delete(const std::string& target) const {
    const auto credentialTarget = CredentialTarget(target);
    if (!credentialTarget.empty()) {
        static_cast<void>(CredDeleteW(credentialTarget.c_str(), CRED_TYPE_GENERIC, 0));
    }
}

std::wstring PanelCredentialVault::CredentialTarget(const std::string& target) const {
    return Utf8ToWide("Pixels." + credentialNamespace_ + "." + target);
}

}  // namespace px::panel::product
