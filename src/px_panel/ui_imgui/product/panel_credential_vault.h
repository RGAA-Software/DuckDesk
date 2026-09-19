#pragma once

#include <memory>
#include <optional>
#include <string>

namespace px::panel::product {

class PanelCredentialVault final {
public:
    static std::shared_ptr<PanelCredentialVault> Create(std::string credentialNamespace = "RemoteDevice");

    explicit PanelCredentialVault(std::string credentialNamespace);

    [[nodiscard]] std::optional<std::string> Read(const std::string& target) const;
    [[nodiscard]] bool Write(const std::string& target, const std::string& password) const;
    void Delete(const std::string& target) const;

private:
    [[nodiscard]] std::wstring CredentialTarget(const std::string& target) const;

    std::string credentialNamespace_{};
};

}  // namespace px::panel::product
